#pragma once

#include <future>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "builder.h"
#include "cache.h"
#include "levelcache.h"
#include "memtable.h"
#include "version.h"
#include "LSMKV/kvstore_api.h"
#include "utils/executor.h"
#include "db_info.h"
#include "LSMKV/dbformat.h"
#include "vlog_builder.h"
#include "utils/cond_var.h"
#include "utils/rwlock.h"


class KVStore : public KVStoreAPI {
private:
    LSMKV::Executor scheduler_{};
    LSMKV::Version *version_;

    LSMKV::DB_Info db_info;
    LSMKV::LevelCache *kc;
    LSMKV::Cache *cache;

    LSMKV::VLogBuilder *vlog_;

    static constexpr int MEM_MAX_SIZE = LSMKV::Option::mem_max_size_;

    static constexpr bool enable_crc_check = true;

    LSMKV::RWLock rwlock_;
    std::shared_ptr<LSMKV::MemTable> mem_;

    std::atomic<bool> bg_catch_error_ = false;

    std::shared_mutex compaction_lock_;
    std::atomic<bool> background_compaction_scheduled_ = false;

    std::atomic<std::shared_ptr<LSMKV::MemTable>> imm_;

    std::atomic<bool> recovering_ = false;

    std::atomic<bool> shutting_down_{false};

    LSMKV::VLogReader vlog_reader_;

    void putWhenGc(key_t key, const LSMKV::Slice &s);

    Status GetOffset(key_t key, LSMKV::VLogEntryInfo *offset);

    Status WriteLevel0Table(std::shared_ptr<LSMKV::MemTable> &&imm);

    [[nodiscard]] const std::string &DBName() const {
        return db_info.dbname;
    }

    [[nodiscard]] const std::string &VLogPath() const {
        return db_info.vlog;
    }

    Status Prepare();

    void RecordBackgroundError(LSMKV::Slice s);

    void BackgroundCall();

    void BackgroundCompaction();

    void CompactMemTable(std::shared_ptr<LSMKV::MemTable> &&imm);

    void CompactSSTable(uint64_t level, uint64_t file_no, uint64_t size);

    void RemoveObsoleteFiles();

    void MaybeScheduleCompaction();

    Status ReadFromVLog();

    Status GetImpl(LSMKV::Slice key, std::string *val);

    Status WriteImpl(LSMKV::Slice key, LSMKV::Slice val, LSMKV::ValueType type);
public:
    KVStore(const std::string &dir, const std::string &vlog);

    KVStore() = delete;

    ~KVStore() override;

    Status put(LSMKV::Slice key, LSMKV::Slice s) override;

    Status get(LSMKV::Slice key, std::string *val) override;

    std::string get(LSMKV::Slice key) override;

    Status del(key_t key) override;

    Status scan(key_t start, key_t end, std::list<std::pair<std::string, std::string>> &result) override;

    void reset() override;

    void gc(uint64_t chunk_size) override;
};
