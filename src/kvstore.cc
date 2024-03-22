#include "kvstore.h"
#include "include/builder.h"
#include <memory>
#include <string>
#include <utility>

#define MB (1024 * 1024)

LSMKV::status KVStoreAPI::Open(const std::string &dir, const std::string &vlog,
                               KVStoreAPI **ptr) {
  std::unique_ptr<KVStoreAPI> store;

  KVStoreAPI::Open(dir, vlog, &store);

  *ptr = store.release();

  return LSMKV::status::OK();
}

LSMKV::status KVStoreAPI::Open(const std::string &dir, const std::string &vlog,
                               std::unique_ptr<KVStoreAPI> *ptr) {
  *ptr = std::make_unique<KVStore>(dir, vlog);
  return LSMKV::status::OK();
}

static inline bool CheckCrc(const char *data, uint32_t len) {
  return data[0] == '\377' &&
         utils::crc16(data + 3, len - 3) == LSMKV::DecodeFixed16(data + 1);
}

KVStore::KVStore(const std::string &dir, const std::string &vlog)
    : db_info(dir, vlog) {
  version_ = new LSMKV::Version(dir);
  vlog_ = new LSMKV::VLogBuilder(vlog);
  future_ = std::nullopt;
  kc = new LSMKV::LevelCache(dir, version_);
  cache = new LSMKV::Cache();
  mem = std::make_unique<LSMKV::MemTable>();
  builder_ = new LSMKV::Builder(db_info, version_, kc);
}

KVStore::~KVStore() {
  if (future_.has_value()) {
    future_->get();
    delete builder_->it_;
  }

  if (mem != nullptr && mem->memoryUsage() != 0) {
    writeLevel0Table(mem.get());
  }

  delete builder_;
  delete version_;
  delete cache;
  delete kc;
  mem.reset();
}

void KVStore::genBuilder() {
  imm = std::move(mem);
  LSMKV::Iterator *iter = imm->newIterator();
  builder_->setAll(imm->memoryUsage(), iter);
  mem = std::make_unique<LSMKV::MemTable>();
}

void KVStore::putWhenGc(key_t key, const LSMKV::Slice &s) {
  if (mem->memoryUsage() >= MEM_MAX_SIZE) {
    if (future_.has_value()) {
      future_->get();
      future_ = std::nullopt;
      imm = nullptr;
      delete builder_->it_;
    }
    writeLevel0Table(mem.get());
    mem = std::make_unique<LSMKV::MemTable>();
  }

  // FIXME:
  //    mem->put(version_->NewSequence(), LSMKV::kTypeValue, key, s);
}

using status = KVStoreAPI::status;

status KVStore::prepare() {
  do {
    if (future_.has_value()) {
      future_->get();
      future_ = std::nullopt;
      imm = nullptr;
      delete builder_->it_;
    } else if (mem->memoryUsage() >= MEM_MAX_SIZE) {
      genBuilder();
      cache->Drop();

      future_ = scheduler_.submit(builder_->create_operator());

      future_->get();
      future_ = std::nullopt;
      imm = nullptr;
      delete builder_->it_;
    } else {
      break;
    }
  } while (true);

  return status::OK();

  uint64_t new_log_number = version_->NewFileNumber();
  LSMKV::WritableFile *lfile = nullptr;
  status s =
      NewWritableFile(LSMKV::LogFileName(DBName(), new_log_number), &lfile);
  if (!s.ok()) {
    // Avoid chewing through file number space in a tight loop.
    version_->ReuseFileNumber(new_log_number);
  }

  delete log_;

  //    return s;
}

/**
 * Insert/Update the key-value pair.
 * No return values for simplicity.
 */

status KVStore::put(LSMKV::Slice key, LSMKV::Slice val) {
  status s = prepare();
  if (!s.ok()) {
    return s;
  } else {

    auto info = vlog_->Append(key, val);

    mem->put(version_->NewSequence(), LSMKV::kTypeValue, key, offset,
             val.size());
  }
  return status::OK();
}

status KVStore::get(LSMKV::Slice key, std::string *val) {
  status status = status::OK();
  LSMKV::SequenceNumber seq = version_->LastSequence();
  LSMKV::Slice rep_;
  if (!mem->get(seq, key, rep_, status)) {
    std::string s;
    if (future_.has_value()) {
      if (imm->get(seq, key, rep_, status)) {
        // TODO: read rep_ from vlog

        *val = std::move(s);
        return status;
      }
      future_->get();
      future_ = std::nullopt;
      imm = nullptr;
      delete builder_->it_;
    }

    if (kc->empty()) {
      return status::notFound();
    }

    if (!((status = kc->get(key, s)).ok())) {
      return status;
    }

    if (s.empty()) {
      return status::notFound();
    }

    LSMKV::Slice result;
    uint32_t len = LSMKV::DecodeFixed32(s.data() + 8);

    if (len == 0) [[unlikely]] {
      return status::notFound();
    }

    const char *data;
    std::unique_ptr<char[]> buf;
    len += 15;

    if (len >= 2 * MB) {
      buf = std::make_unique<char[]>(len);
      LSMKV::RandomReadableFile *file;
      LSMKV::NewRandomReadableFile(VLogPath(), &file);
      file->Read(LSMKV::DecodeFixed64(s.data()), len, &result, buf.get());
      delete file;
    } else [[likely]] {
      auto offset = LSMKV::DecodeFixed64(s.data());
      if (!cache->Get(offset, len, result)) {
        cache->Drop();
        LSMKV::MemoryReadableFile *file;
        if (!LSMKV::NewRandomMemoryReadFile(VLogPath(), 4 * MB, offset,
                                            &file)) {
          buf = std::make_unique<char[]>(len);
          LSMKV::RandomReadableFile *file_;
          LSMKV::NewRandomReadableFile(VLogPath(), &file_);
          file_->Read(LSMKV::DecodeFixed64(s.data()), len, &result, buf.get());
          delete file;
        } else {
          cache->Push(file);
          cache->Get(offset, len, result);
        }
      }
    }

    data = result.data();
    if (result.size() <= 15) [[unlikely]] {
      return status::notFound();
    }

    if constexpr (enable_crc_check) {
      if (!CheckCrc(data, len)) {
        return status::corruption();
      }
    }

    len -= LSMKV::Option::k_vlog_header_size_;
    val->resize(len);
    memcpy(val->data(), data + LSMKV::Option::k_vlog_header_size_, len);
    assert(status.ok());
    assert(*val != "~DELETED~");
    assert(!val->empty());
    return status;
  }

  // TODO: read rep_ from vlog

  if (status.is_not_found()) {
    return status;
  }

  return status;
}

std::string KVStore::get(LSMKV::Slice key) {
  std::string res;
  status s = get(key, &res);

  if (s.ok()) {
    return res;
  }

  return "";
}

status KVStore::del(key_t key) {
  std::string val;
  status status = get(key, &val);

  if (status.is_not_found()) {
    return status::notFound();
  }

  if (!status.ok()) {
    return status;
  }

  status = prepare();
  if (!status.ok()) {
    return status;
  } else {
    mem->put(version_->NewSequence(), LSMKV::kTypeDeletion, key, 0, 0);
  }
  return status::OK();
}

/**
 * This resets the kvstore. All key-value pairs should be removed,
 * including memtable and all sstables files.
 */
void KVStore::reset() {
  if (future_.has_value()) {
    future_->get();
    future_ = std::nullopt;
    delete builder_->it_;
  }

  imm = nullptr;
  cache->Drop();

  utils::rmfiles(DBName());
  version_->reset();
  kc->reset();
  mem = std::make_unique<LSMKV::MemTable>();
}

/**
 * Return a list including all the key-value pair between key1 and key2.
 * keys in the list should be in an ascending order.
 * An empty string indicates not found.
 */
status KVStore::scan(key_t key1, key_t key2,
                     std::list<std::pair<std::string, std::string>> &list) {
  LSMKV::Iterator *iter = mem->newIterator();
  std::list<std::pair<std::string, std::string>> tmp_list;
  iter->scan(key1, key2, tmp_list);
  if (future_.has_value()) {
    future_->get();
    future_ = std::nullopt;
    imm = nullptr;
    delete builder_->it_;
  }

  std::map<std::string, std::string> map;
  kc->scan(key1, key2, map);

  LSMKV::RandomReadableFile *files;
  char buf[1024];
  std::map<std::string, std::string> tmp_map;
  tmp_map.insert(tmp_list.begin(), tmp_list.end());
  LSMKV::NewRandomReadableFile(VLogPath(), &files);
  std::unique_ptr<LSMKV::RandomReadableFile> file(files);
  for (auto &it : map) {
    LSMKV::Slice result;
    uint32_t len = LSMKV::DecodeFixed32(it.second.data() + 8) + 15;
    if (len == 15) {
      continue;
    }
    if (len <= 1024) {
      file->Read(LSMKV::DecodeFixed64(it.second.data()), len, &result, buf);
      if (enable_crc_check && !CheckCrc(result.data(), len)) [[unlikely]] {
        continue;
      }
      result.remove_prefix(15);
      it.second = std::move(result.toString());
    } else {
      std::unique_ptr<char[]> large_buf = std::make_unique<char[]>(len);
      file->Read(LSMKV::DecodeFixed64(it.second.data()), len, &result,
                 large_buf.get());
      if (enable_crc_check && !CheckCrc(result.data(), len)) [[unlikely]] {
        continue;
      }
      result.remove_prefix(15);
      it.second = std::move(result.toString());
    }
  }
  tmp_map.merge(map);
  for (auto &it : tmp_map) {
    if (LSMKV::ExtractValueType(it.first) == LSMKV::kTypeValue) {
      list.emplace_back(LSMKV::ExtractUserKey(it.first), it.second);
    }
  }
  delete iter;
  return status::OK();
}

bool KVStore::GetOffset(key_t key, uint64_t &offset) {
  if (!mem->contains(key)) {
    return kc->GetOffset(key, offset).ok();
  }
  return false;
}

/**
 * This reclaims space from vLog by moving valid value and discarding invalid
 * value. chunk_size is the _size in byte you should AT LEAST recycle.
 */
void KVStore::gc(uint64_t chunk_size) {
  if (future_.has_value()) {
    future_->get();
    future_ = std::nullopt;
    imm = nullptr;
    delete builder_->it_;
  }
  cache->Drop();
  LSMKV::RandomReadableFile *files;
  LSMKV::NewRandomReadableFile(VLogPath(), &files);
  std::unique_ptr<LSMKV::RandomReadableFile> file(files);

  // TODO NOT OVERFLOW
  LSMKV::Slice result, value;
  auto size = version_->head - version_->tail;
  if (chunk_size > size) {
    chunk_size = size;
  }

  int factor = chunk_size * 2 < chunk_size ? 1 : 2;
  uint64_t current_size = 0;
  uint64_t vlen = 0;
  uint64_t offset = 0;
  uint32_t len;
  LSMKV::Slice key;
  char *ptr;
  uint64_t _chunk_size = chunk_size * factor;
  std::string value_buf;
  std::unique_ptr<char[]> tmp;
  tmp = std::make_unique<char[]>(_chunk_size);

  // I FORGET TO WRITE COMMENT!
  // NOW I DON'T KNOW HOW I DO THIS
  while (current_size < chunk_size) {
    // value_buf is the start of ceil piece of value
    if (!value_buf.empty()) {
      tmp = std::make_unique<char[]>(vlen);
      file->Read(version_->tail + _chunk_size, vlen, &result, tmp.get());
      if (result.size() < vlen) {
        break;
      }
    } else {
      // READ A _CHUNK_SIZE(ALWAYS TWICE THAN _CHUNK_SIZE)
      file->Read(version_->tail, _chunk_size, &result, tmp.get());
      _chunk_size = result.size();
      if (current_size != 0) {
        break;
      }
      if (_chunk_size == 0) {
        break;
      }
    }
    while (current_size < chunk_size) {
      if (!value_buf.empty()) {
        // APPEND THE NEXT PART OF CEIL PIECE
        value_buf.append(tmp.get(), vlen);
        ptr = &value_buf[0];
        len = value_buf.size() - 15;
        assert(len == LSMKV::DecodeFixed64(ptr + 3));
        // TODO: variable length key
        key = {&value_buf[3], 8};
      } else {
        // CURRENT PTR IN TMP
        ptr = current_size + tmp.get();

        // TODO: WHEN FACTOR == 1 , COULDN'T READ THE LEN
        // THE LEN OF VALUE
        len = LSMKV::DecodeFixed32(ptr + 11);
        // THE KEY OF VALUE
        // TODO: variable length key
        key = {ptr + 3, 8};

        // CANT FETCH ALL BYTES IN _CHUNK_SIZE
        if (len + current_size + 15 > _chunk_size) {
          // vlen is the remaining part length
          vlen = len + current_size + 15 - _chunk_size;
          // STORE THE FIRST PART OF CEIL PIECE
          value_buf.append(ptr, len + 15 - vlen);
          break;
        }
      }
      // DO CRC CHECK AND CHECK WHERE IT IS THE NEWEST VALUE
      if (CheckCrc(ptr, len + 15) && GetOffset(key, offset) &&
          (offset == version_->tail + current_size)) {
        value = LSMKV::Slice(ptr + 15, len);
        putWhenGc(key, value);
      }
      current_size += len + 15;
    }
  }
  assert(current_size <= INT64_MAX);
  assert(version_->tail <= INT64_MAX);
  assert(current_size < INT64_MAX);
  assert(version_->tail < INT64_MAX);
  file = nullptr;
  if (current_size != 0) {
    int st = utils::de_alloc_file(VLogPath(), version_->tail, current_size);
    assert(st == 0);
  }

  version_->tail += current_size;
  if (mem->memoryUsage() != 0) {
    if (future_.has_value()) {
      future_->get();
      delete builder_->it_;
      imm = nullptr;
    }

    genBuilder();
    cache->Drop();

    future_ = scheduler_.submit(builder_->create_operator());
  }
}

int KVStore::writeLevel0Table(LSMKV::MemTable *memtable) {
  LSMKV::Iterator *iter = memtable->newIterator();
  BuildTable(db_info, version_, iter, memtable->memoryUsage(), kc);
  delete iter;
  return 0;
}
