#include "kvstore.h"
#include "include/builder.h"
#include <memory>
#include <string>
#include <utility>
#include "utils/log.h"

#define MB (1024 * 1024)

LSMKV::Status KVStoreAPI::Open(const std::string &dir, const std::string &vlog,
                               KVStoreAPI **ptr) {
    std::unique_ptr<KVStoreAPI> store;

    KVStoreAPI::Open(dir, vlog, &store);

    auto store_ptr = store.release();

    if (store_ptr == nullptr) {
        return Status::IOError("failed to open kvstore");
    }

    *ptr = store_ptr;

    return Status::OK();
}

LSMKV::Status KVStoreAPI::Open(const std::string &dir, const std::string &vlog,
                               std::unique_ptr<KVStoreAPI> *ptr) {
    *ptr = std::make_unique<KVStore>(dir, vlog);
    return Status::OK();
}

KVStore::KVStore(const std::string &dir, const std::string &vlog)
        : db_info(dir, vlog), mem_(std::make_shared<LSMKV::MemTable>()), imm_(nullptr), vlog_reader_(dir) {
    version_ = new LSMKV::Version(DBName());
    vlog_ = new LSMKV::VLogBuilder(DBName());
    vlog_->Open(0);
    kc = new LSMKV::LevelCache(dir, version_, &compaction_lock_);
    cache = new LSMKV::Cache();
}

KVStore::~KVStore() {
    std::unique_lock lock(rwlock_);
    std::unique_lock flock(compaction_lock_);

    delete version_;
    delete cache;
    delete kc;
}

void KVStore::putWhenGc(key_t key, const LSMKV::Slice &s) {
//    if (mem->memoryUsage() >= MEM_MAX_SIZE) {
//        if (future_.has_value()) {
//            future_->get();
//            future_ = std::nullopt;
//            imm = nullptr;
//            delete builder_->it_;
//        }
//        writeLevel0Table(mem.get());
//        mem_ = std::make_unique<LSMKV::MemTable>();
//    }

//    mem_->put(version_->NewSequence(), LSMKV::kTypeValue, key, s);
}

using Status = KVStoreAPI::Status;

void KVStore::MaybeScheduleCompaction() {
    if (background_compaction_scheduled_) {
        // Already scheduled
    } else if (shutting_down_.load(std::memory_order_acquire)) {
        // DB is being deleted; no more background compactions
    } else if (bg_catch_error_.load(std::memory_order_acquire)) {
        // Already got an error; no more changes
    } else if (imm_.load(std::memory_order_acquire) == nullptr) {
        // No work to be done
    } else {
        background_compaction_scheduled_.store(true, std::memory_order_release);
        scheduler_.submit([this]() { this->BackgroundCall(); });
    }
}

void KVStore::BackgroundCall() {
    if (shutting_down_.load(std::memory_order_acquire)) {
        // No more background work when shutting down.
    } else {
        BackgroundCompaction();
    }


    background_compaction_scheduled_.store(false, std::memory_order_release);
    // Previous compaction may have produced too many files in a level,
    // so reschedule another compaction if needed.
    MaybeScheduleCompaction();
    background_compaction_scheduled_.notify_one();
}

void KVStore::BackgroundCompaction() {
    std::unique_lock lock(compaction_lock_);

    if (auto imm = imm_.load(std::memory_order_acquire); imm != nullptr) {
        CompactMemTable(std::move(imm));
        return;
    }

    // Compact SST files

    if (version_->NumLevelFiles(0) >= LSMKV::Option::kL0_CompactionTrigger) {
        // Compact level 0
    }


}

void KVStore::CompactMemTable(std::shared_ptr<LSMKV::MemTable> &&imm) {
    if (imm == nullptr) {
        return;
    }

    // though there is only one thread to write compact imm,
    // but we still need to hold imm_ to prevent the failure of the following code

    // Save the contents of the memtable as a new Table
//    VersionEdit edit;
//    LSMKV::Version *base = version_;
    Status s = WriteLevel0Table(std::move(imm));

    if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
        s = Status::IOError(LSMKV::log::current_info("Deleting DB during memtable compaction"));
    }

    // Replace immutable memtable with the generated Table
//    if (s.ok()) {
//        edit.SetPrevLogNumber(0);
//        edit.SetLogNumber(logfile_number_);  // Earlier logs no longer needed
//        s = versions_->LogAndApply(&edit, &mutex_);
//    }

    if (s.ok()) {
        // TODO: APPLY THE EDIT TO VERSION
//        s = version_->LogAndApply(nullptr);
    }

    if (s.ok()) {
        // Commit to the new state
        imm_.store(nullptr, std::memory_order_release);
        // TODO: replace below with a remove trigger
//        RemoveObsoleteFiles();
    } else {
        RecordBackgroundError(s.ToString());

        background_compaction_scheduled_.store(false, std::memory_order_release);
        background_compaction_scheduled_.notify_one();
    }
}

Status KVStore::Prepare() {
    bool allow_delay = true;
    Status s = Status::OK();

    do {
        if (bg_catch_error_.load(std::memory_order_acquire)) {
            s = Status::BGError();
            break;
        } else if (allow_delay && version_->NumLevelFiles(0) >= LSMKV::Option::kL0_CompactionTrigger) {
            rwlock_.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            allow_delay = false;
            rwlock_.lock();
        } else if (mem_->memoryUsage() <= MEM_MAX_SIZE) {
            break;
        } else if (auto imm = imm_.load(std::memory_order_acquire); imm != nullptr) {
            // We have filled up the current memtable, but the previous is still being compacted
            rwlock_.atomic_wait(background_compaction_scheduled_, true);
        } else if (version_->NumLevelFiles(0) >= LSMKV::Option::kL0_CompactionTrigger) {
            rwlock_.atomic_wait(background_compaction_scheduled_, true);
        } else {
            assert(imm_.load(std::memory_order_acquire) == nullptr);
            imm_.store(std::move(mem_), std::memory_order_release);
            mem_ = std::make_shared<LSMKV::MemTable>();
            MaybeScheduleCompaction();
        }
    } while (true);

    return s;
}

/**
 * Insert/Update the key-value pair.
 * No return values for simplicity.
 */

Status KVStore::put(LSMKV::Slice key, LSMKV::Slice val) {
    rwlock_.lock();
    Status s = Prepare();

    if (!s.ok()) {
        rwlock_.unlock();
        return s;
    } else {
        return WriteImpl(key, val, LSMKV::kTypeValue);
    }
}

Status KVStore::WriteImpl(LSMKV::Slice key, LSMKV::Slice val, LSMKV::ValueType type) {
    auto last_seq = version_->LastSequence();
    Status s = Status::OK();

    LSMKV::VLogEntryInfo info{};

    LSMKV::SequenceNumber seq = (last_seq << 8) | type;
    if (type == LSMKV::kTypeDeletion) {
        s = mem_->put(seq, key, {});
    } else {
        s = vlog_->Append(seq, key, val, &info);

        if (s.ok()) {
            LSMKV::Slice info_slice(reinterpret_cast<char *>(&info), sizeof(info));
            s = mem_->put(seq, key, info_slice);
        }
    }

    if (s.ok()) {
        version_->SetLastSequence(last_seq + 1);
    }

    rwlock_.unlock();

    return s;
}

Status KVStore::GetImpl(LSMKV::Slice key, std::string *val) {
    Status status = Status::OK();

    LSMKV::SequenceNumber seq = version_->LastSequence();
    LSMKV::VLogEntryInfo rep_{};
    LSMKV::QueryKey query_key{seq, key};
    if (!mem_->get(query_key, &rep_, &status)) {
        std::string s;

        auto imm = imm_.load(std::memory_order_acquire);

        if (imm != nullptr) {
            if (imm->get(query_key, &rep_, &status)) {
                *val = std::move(s);
                return status;
            }
        }

        if (kc->empty()) {
            return Status::NotFound();
        }

        status = kc->get(query_key, &rep_);
    }

    if (status.ok()) {
        if (rep_.length_ == 0) {
            return Status::NotFound();
        }

        return vlog_reader_.Read(rep_, val);
    }

    return status;
}


Status KVStore::get(LSMKV::Slice key, std::string *val) {
    std::shared_lock lock(rwlock_);

    return GetImpl(key, val);
}

std::string KVStore::get(LSMKV::Slice key) {
    std::string res;
    Status s = get(key, &res);

    if (s.ok()) {
        return res;
    }

    return "";
}

Status KVStore::del(key_t key) {
    std::string val;
    Status status = Status::OK();

    {
        std::unique_lock rwlock_guard(rwlock_);

        status = GetImpl(key, &val);

        if (status.IsNotFound()) {
            return Status::NotFound();
        }

        if (!status.ok()) {
            return status;
        }
        rwlock_guard.release();

    }

    status = Prepare();
    if (!status.ok()) {
        rwlock_.unlock();
        return status;
    } else {
        return WriteImpl(key, LSMKV::Slice(nullptr, 0), LSMKV::kTypeDeletion);
    }
}

/**
 * This resets the kvstore. All key-value pairs should be removed,
 * including memtable and all sstables files.
 */
void KVStore::reset() {
    std::unique_lock lock(rwlock_);
    std::unique_lock compaction_lock(compaction_lock_);

    imm_ = nullptr;

    utils::rmfiles(DBName());
    vlog_->reset();
    vlog_->Open(0);
    version_->reset();
    kc->reset();
    mem_ = std::make_shared<LSMKV::MemTable>();
}

/**
 * Return a list including all the key-value pair between key1 and key2.
 * keys in the list should be in an ascending order.
 * An empty string indicates not found.
 */
Status KVStore::scan(key_t key1, key_t key2,
                     std::list<std::pair<std::string, std::string>> &list) {
    std::list<std::pair<std::string, std::string>> tmp_list;
    {
        std::shared_ptr<LSMKV::MemTable> imm;
        {
            std::shared_lock lock(rwlock_);
            LSMKV::Iterator *iter = mem_->newIterator();
            imm = imm_.load(std::memory_order_acquire);
            iter->seek(key1, key2);

            while (iter->hasNext()) {
                tmp_list.emplace_back(iter->key(), iter->value());
                iter->next();
            }

            delete iter;
        }

        {
            if (imm != nullptr) {
                LSMKV::Iterator *iter_ = imm->newIterator();
                std::list<std::pair<std::string, std::string>> tmp_list_;
                iter_->seek(key1, key2);

                while (iter_->hasNext()) {
                    tmp_list_.emplace_back(iter_->key(), iter_->value());
                    iter_->next();
                }

                tmp_list.merge(tmp_list_);
                delete iter_;
            }
        }
    }


    std::map<std::string, std::string> map;
    kc->scan(key1, key2, map);

    LSMKV::RandomReadableFile *files;
    std::map<std::string, std::string> tmp_map;
    tmp_map.insert(tmp_list.begin(), tmp_list.end());

    tmp_map.merge(map);
    for (auto &it: tmp_map) {
        if (LSMKV::ExtractValueType(it.first) == LSMKV::kTypeValue) {


            list.emplace_back(LSMKV::ExtractUserKey(it.first), it.second);
        }
    }
    return Status::OK();
}

Status KVStore::GetOffset(key_t key, LSMKV::VLogEntryInfo *offset) {
    std::shared_lock lock(rwlock_);
    Status s;
//    auto t = mem_->get(version_->LastSequence(), key, *offset);
//    if (!mem_->contains(key)) {
//        return kc->GetOffset(key, offset).ok();
//    }
//    return false;
}

/**
 * This reclaims space from vLog by moving valid value and discarding invalid
 * value. chunk_size is the _size in byte you should AT LEAST recycle.
 */
void KVStore::gc(uint64_t chunk_size) {
    assert(false && "not implemented");
//    // should stop the world
//    // must lock rwlock_ before compaction_lock_
//    std::unique_lock lock(rwlock_);
//    std::unique_lock compaction_lock(compaction_lock_);
//
//
//    cache->Drop();
//    LSMKV::RandomReadableFile *files;
//    LSMKV::NewRandomReadableFile(VLogPath(), &files);
//    std::unique_ptr<LSMKV::RandomReadableFile> file(files);
//
//    // TODO NOT OVERFLOW
//    LSMKV::Slice result, value;
//    auto size = version_->head - version_->tail;
//    if (chunk_size > size) {
//        chunk_size = size;
//    }
//
//    int factor = chunk_size * 2 < chunk_size ? 1 : 2;
//    uint64_t current_size = 0;
//    uint64_t vlen = 0;
//    uint64_t offset = 0;
//    uint32_t len;
//    LSMKV::Slice key;
//    char *ptr;
//    uint64_t _chunk_size = chunk_size * factor;
//    std::string value_buf;
//    std::unique_ptr<char[]> tmp;
//    tmp = std::make_unique<char[]>(_chunk_size);
//
//    // I FORGET TO WRITE COMMENT!
//    // NOW I DON'T KNOW HOW I DO THIS
//    while (current_size < chunk_size) {
//        // value_buf is the start of ceil piece of value
//        if (!value_buf.empty()) {
//            tmp = std::make_unique<char[]>(vlen);
//            file->Read(version_->tail + _chunk_size, vlen, &result, tmp.get());
//            if (result.size() < vlen) {
//                break;
//            }
//        } else {
//            // READ A _CHUNK_SIZE(ALWAYS TWICE THAN _CHUNK_SIZE)
//            file->Read(version_->tail, _chunk_size, &result, tmp.get());
//            _chunk_size = result.size();
//            if (current_size != 0) {
//                break;
//            }
//            if (_chunk_size == 0) {
//                break;
//            }
//        }
//        while (current_size < chunk_size) {
//            if (!value_buf.empty()) {
//                // APPEND THE NEXT PART OF CEIL PIECE
//                value_buf.append(tmp.get(), vlen);
//                ptr = &value_buf[0];
//                len = value_buf.size() - 15;
//                assert(len == LSMKV::DecodeFixed64(ptr + 3));
//                // TODO: variable length key
//                key = {&value_buf[3], 8};
//            } else {
//                // CURRENT PTR IN TMP
//                ptr = current_size + tmp.get();
//
//                // TODO: WHEN FACTOR == 1 , COULDN'T READ THE LEN
//                // THE LEN OF VALUE
//                len = LSMKV::DecodeFixed32(ptr + 11);
//                // THE KEY OF VALUE
//                // TODO: variable length key
//                key = {ptr + 3, 8};
//
//                // CANT FETCH ALL BYTES IN _CHUNK_SIZE
//                if (len + current_size + 15 > _chunk_size) {
//                    // vlen is the remaining part length
//                    vlen = len + current_size + 15 - _chunk_size;
//                    // STORE THE FIRST PART OF CEIL PIECE
//                    value_buf.append(ptr, len + 15 - vlen);
//                    break;
//                }
//            }
//            // DO CRC CHECK AND CHECK WHERE IT IS THE NEWEST VALUE
//            if (CheckCrc(ptr, len + 15) && GetOffset(key, offset) &&
//                (offset == version_->tail + current_size)) {
//                value = LSMKV::Slice(ptr + 15, len);
//                putWhenGc(key, value);
//            }
//            current_size += len + 15;
//        }
//    }
//    assert(current_size <= INT64_MAX);
//    assert(version_->tail <= INT64_MAX);
//    assert(current_size < INT64_MAX);
//    assert(version_->tail < INT64_MAX);
//    file = nullptr;
//    if (current_size != 0) {
//        int st = utils::de_alloc_file(VLogPath(), version_->tail, current_size);
//        assert(st == 0);
//    }
//
//    version_->tail += current_size;
//    if (mem_->memoryUsage() != 0) {
//
//
//        genBuilder();
//        cache->Drop();
//
//        future_ = scheduler_.submit(builder_->create_operator());
//    }
}

Status KVStore::WriteLevel0Table(std::shared_ptr<LSMKV::MemTable> &&imm) {
    // TODO: make all below without lock
    LSMKV::SSTFileMeta meta;
    meta.file_number_ = version_->NewFileNumber();

    LSMKV::Iterator *iter = imm->newIterator();

    Status s = Status::OK();
    iter->seekToFirst();


    // Write a new SST file
    s = LSMKV::BuildTable(db_info, version_, iter, &meta);


    delete iter;

    // Note that if file_size is zero, the file has been deleted and
    // should not be added to the manifest.
    int level = 0;
    if (s.ok() && meta.file_size_ > 0) {
        const LSMKV::Slice min_user_key = meta.smallest.user_key();
        const LSMKV::Slice max_user_key = meta.largest.user_key();
//        if (base != nullptr) {
//            level = base->PickLevelForMemTableOutput(min_user_key, max_user_key);
//        }
        kc->AddFile(level, meta.file_number_, meta.file_size_, meta.smallest,
                    meta.largest);


    }

//    CompactionStats stats;
//    stats.micros = env_->NowMicros() - start_micros;
//    stats.bytes_written = meta.file_size;
//    stats_[level].Add(stats);
    return s;
}

void KVStore::RecordBackgroundError(LSMKV::Slice s) {
    if (!bg_catch_error_.load(std::memory_order_acquire)) {
        LSMKV::log::error("Background error: {}", s);

        // FIXME: thread wait in condition, but the condition is still not notified
    }
}

void KVStore::RemoveObsoleteFiles() {
    if (bg_catch_error_) {
        return;
    }

//    // Make a set of all of the live files
//    std::set<uint64_t> live = pending_outputs_;
//    versions_->AddLiveFiles(&live);
//
//    std::vector<std::string> filenames;
//    env_->GetChildren(dbname_, &filenames);  // Ignoring errors on purpose
//    uint64_t number;
//    FileType type;
//    std::vector<std::string> files_to_delete;
//    for (std::string &filename: filenames) {
//        if (ParseFileName(filename, &number, &type)) {
//            bool keep = true;
//            switch (type) {
//                case kLogFile:
//                    keep = ((number >= versions_->LogNumber()) ||
//                            (number == versions_->PrevLogNumber()));
//                    break;
//                case kDescriptorFile:
//                    // Keep my manifest file, and any newer incarnations'
//                    // (in case there is a race that allows other incarnations)
//                    keep = (number >= versions_->ManifestFileNumber());
//                    break;
//                case kTableFile:
//                    keep = (live.find(number) != live.end());
//                    break;
//                case kTempFile:
//                    // Any temp files that are currently being written to must
//                    // be recorded in pending_outputs_, which is inserted into "live"
//                    keep = (live.find(number) != live.end());
//                    break;
//                case kCurrentFile:
//                case kDBLockFile:
//                case kInfoLogFile:
//                    keep = true;
//                    break;
//            }
//
//            if (!keep) {
//                files_to_delete.push_back(std::move(filename));
//                if (type == kTableFile) {
//                    table_cache_->Evict(number);
//                }
//                Log(options_.info_log, "Delete type=%d #%lld\n", static_cast<int>(type),
//                    static_cast<unsigned long long>(number));
//            }
//        }
//    }
//
//    // While deleting all files unblock other threads. All files being deleted
//    // have unique names which will not collide with newly created files and
//    // are therefore safe to delete while allowing other threads to proceed.
//    mutex_.Unlock();
//    for (const std::string &filename: files_to_delete) {
//        env_->RemoveFile(dbname_ + "/" + filename);
//    }
//    mutex_.Lock();
}
