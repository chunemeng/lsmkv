#include "kvstore.h"

#include <memory>
#include <string>
#include <utility>
#include "utils/log.h"
#include "include/builder.h"


#define MB (1024 * 1024)

using Status = KVStoreAPI::Status;

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
        : db_info(dir, vlog), mem_(std::make_shared<LSMKV::MemTable>()), imm_(nullptr),
          version_(std::make_shared<LSMKV::Version>(DBName())), vlog_reader_(dir, version_) {
    vlog_ = std::make_unique<LSMKV::VLogBuilder>(DBName(), version_->executor_);
    auto vlog_no = version_->NewVLogFileNumber();
    vlog_->Open(vlog_no);
    kc = std::make_unique<LSMKV::LevelCache>(dir, version_, &compaction_lock_);
}

KVStore::~KVStore() {
    shutting_down_.store(true, std::memory_order_release);
    scheduler_.Shutdown();
    std::unique_lock lock(rwlock_);
    std::unique_lock compaction_lock(compaction_lock_);

    version_ = nullptr;
    mem_ = nullptr;
    imm_ = nullptr;
    vlog_ = nullptr;
    kc = nullptr;
}


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
    }

    // Compact SST files

    if (kc->NumLevelFiles(0) < LSMKV::Option::kL0_CompactionTrigger && !triger_sst_compaction_) {
        return;
    }

    Status s = Status::OK();

    s = CompactSSTFile(0, true);

    if (!s.ok()) {
        RecordBackgroundError(s.ToString());
        background_compaction_scheduled_.store(false, std::memory_order_release);
        background_compaction_scheduled_.notify_one();
    }
}

void KVStore::CompactMemTable(std::shared_ptr<LSMKV::MemTable> &&imm) {
    if (imm == nullptr) {
        return;
    }

    Status s = WriteLevel0Table(std::move(imm));

    if (s.ok() && shutting_down_.load(std::memory_order_acquire)) {
        s = Status::IOError(LSMKV::log::current_info("Deleting DB during memtable compaction"));
    }


    if (s.ok()) {
        imm_.store(nullptr, std::memory_order_release);
    } else {
        RecordBackgroundError(s.ToString());

        background_compaction_scheduled_.store(false, std::memory_order_release);
        background_compaction_scheduled_.notify_one();
    }
}

Status KVStore::CompactSSTFile(uint32_t level, bool may_trigger_next_compaction) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return Status::OK();
    }

    std::unique_ptr<LSMKV::CompactInfo> compact_info = std::make_unique<LSMKV::CompactInfo>();

    compact_info->level = level;

    auto status = kc->PickCompaction(compact_info.get());

    if (!status.ok()) {
        return status;
    }

    if (compact_info->old_files.empty()) {
        return status;
    }

    status = kc->DoCompactionWork(compact_info.get());

    if (!status.ok()) {
        return status;
    }

    return CompactSSTFile(level + 1, may_trigger_next_compaction);
}


Status KVStore::Prepare() {
    bool allow_delay = true;
    Status s = Status::OK();

    do {
        if (bg_catch_error_.load(std::memory_order_acquire)) {
            s = Status::BGError();
            break;
        } else if (allow_delay && kc->NumLevelFiles(0) >= LSMKV::Option::kL0_CompactionTrigger) {
            rwlock_.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            allow_delay = false;
            rwlock_.lock();
        } else if (mem_->memoryUsage() <= MEM_MAX_SIZE) {
            break;
        } else if (imm_.load(std::memory_order_acquire) != nullptr) {
            // We have filled up the current memtable, but the previous is still being compacted
            rwlock_.atomic_wait(background_compaction_scheduled_, true);
        } else if (kc->NumLevelFiles(0) >= LSMKV::Option::kL0_CompactionTrigger) {
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
    std::unique_lock lock(rwlock_, std::adopt_lock);
    auto last_seq = version_->LastSequence();
    Status s = Status::OK();

    LSMKV::VLogEntryInfo info{};

    LSMKV::SequenceNumber seq = (last_seq << 8) | type;

    if (vlog_->Full(val.size())) {
        s = vlog_->Close();

        auto vlog_no = version_->NewVLogFileNumber();

        if (s.ok()) {
            vlog_->Open(vlog_no);
        }

        if (!s.ok()) {
            version_->ReuseVLogFileNumber(vlog_no);
            return s;
        }
    }

    // wal
    s = vlog_->Append(seq, key, val, &info);

    if (s.ok()) {
        if (type == LSMKV::kTypeDeletion) {
            s = mem_->put(seq, key, {});
        } else {
            LSMKV::Slice info_slice(reinterpret_cast<char *>(&info), sizeof(info));
            s = mem_->put(seq, key, info_slice);
        }

        if (s.ok()) {
            version_->SetLastSequence(last_seq + 1);
        }
    }

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
    } else if (!s.IsNotFound()) {
        LSMKV::log::error("get error: {}", s.ToString());
    }
    return {};
}

Status KVStore::del(LSMKV::Slice key) {
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

    version_->reset();

    vlog_->reset();
    auto vlog_no = version_->NewVLogFileNumber();

    vlog_->Open(vlog_no);

    kc->reset();
    mem_ = std::make_shared<LSMKV::MemTable>();
}

/**
 * Return a list including all the key-value pair between key1 and key2.
 * keys in the list should be in an ascending order.
 * An empty string indicates not found.
 */
Status KVStore::scan(LSMKV::Slice key1, LSMKV::Slice key2,
                     std::list<std::pair<std::string, std::string>> &list) {
    auto seq = version_->LastSequence();
    LSMKV::QueryKey query_key1{seq, key1};
    LSMKV::QueryKey query_key2{seq, key2};


    std::map<std::string, std::string> map;
    {

        std::string last_key;
        std::shared_ptr<LSMKV::MemTable> imm;
        {
            std::shared_lock lock(rwlock_);
            LSMKV::Iterator *iter = mem_->newIterator();
            imm = imm_.load(std::memory_order_acquire);
            iter->seek(query_key1.mem_key(), query_key2.mem_key());

            LSMKV::Scan(&map, iter, seq);

            delete iter;
        }

        {
            if (imm != nullptr) {
                LSMKV::Iterator *iter = imm->newIterator();
                iter->seek(key1, key2);

                LSMKV::Scan(&map, iter, seq);

                delete iter;
            }
        }
    }


    kc->scan(query_key1.internal_key(), query_key2.internal_key(), &map);


    for (auto &it: map) {
        list.emplace_back(it.first, it.second);
    }
    Status status = Status::OK();
    for (auto it = list.begin(); it != list.end(); it++) {
        if (it->second.empty()) {
            list.erase(it);
            continue;
        }
        LSMKV::VLogEntryInfo info{};
        status = info.Decode(it->second);
        if (!status.ok()) [[unlikely]] {
            break;
        }
        status = vlog_reader_.Read(info, &it->second);
        if (!status.ok()) [[unlikely]] {
            if (status.IsExpired()) {
                list.erase(it);
            } else {
                break;
            }
        }
    }

    return status;
}

/**
 * This reclaims space from vLog by moving valid value and discarding invalid
 * value. chunk_size is the _size in byte you should AT LEAST recycle.
 */
Status KVStore::gc() {
    return kc->gc();
}

Status KVStore::WriteLevel0Table(std::shared_ptr<LSMKV::MemTable> &&imm) {
    // TODO: make all below without lock
    LSMKV::SSTFileMeta meta;

    LSMKV::Iterator *iter = imm->newIterator();

    Status s = Status::OK();
    iter->seekToFirst();


    // Write a new SST file
    s = LSMKV::BuildTable(db_info, version_.get(), iter, &meta);


    delete iter;

    // Note that if file_size is zero, the file has been deleted and
    // should not be added to the manifest.
    if (s.ok() && meta.file_size_ > 0) {
        version_->SetSSTFileNumber(meta.file_number_ + 1);

        const LSMKV::Slice min_user_key = meta.smallest.user_key();
        const LSMKV::Slice max_user_key = meta.largest.user_key();
//        if (base != nullptr) {
//            level = base->PickLevelForMemTableOutput(min_user_key, max_user_key);
//        }
        kc->AddFile(0, &meta);
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

        background_compaction_scheduled_.store(false, std::memory_order_release);
        background_compaction_scheduled_.notify_one();
    }
}

uint64_t KVStore::ApproximateVLogFileSize() const {
    uint64_t size = 0;
    auto f_no = version_->last_vlog_file_no_.load(std::memory_order_acquire);

    for (auto i = f_no; i > 0; i--) {
        auto fname = LSMKV::VLogFileName(DBName(), i);

        if (LSMKV::FileExists(fname)) {
            auto res = LSMKV::GetFileSize(fname);
            if (res > 0) {
                size += res;
            }
        }
    }

    return size;
}

Status KVStore::ExpireAt(LSMKV::SequenceNumber seq) {
    auto cur_seq = version_->LastSequence();

    if (seq > cur_seq) {
        return Status::Corruption("seq is larger than current seq");
    }

    auto last_live_seq = version_->LastLivingSequence();
    if (seq < last_live_seq) {
        return Status::OK();
    }

    version_->SetLastLivingSequence(seq + 1);

    return Status::OK();
}
