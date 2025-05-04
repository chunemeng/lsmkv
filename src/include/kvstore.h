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

    LSMKV::DB_Info db_info;

    std::shared_ptr<LSMKV::Version> version_;

    std::unique_ptr<LSMKV::LevelCache> kc;

    std::unique_ptr<LSMKV::VLogBuilder> vlog_;

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

    bool triger_sst_compaction_ = false;

    LSMKV::VLogReader vlog_reader_;

    Status WriteLevel0Table(std::shared_ptr<LSMKV::MemTable> &&imm);

    [[nodiscard]] const std::string &DBName() const {
        return db_info.dbname;
    }

    [[nodiscard]] const std::string &VLogPath() const {
        return db_info.vlog;
    }

    Status Prepare();

    Status Recover();

    void RecordBackgroundError(LSMKV::Slice s);

    void BackgroundCall();

    void BackgroundCompaction();

    void CompactMemTable(std::shared_ptr<LSMKV::MemTable> &&imm);

    Status CompactSSTFile(uint32_t level, bool may_trigger_next_compaction);

    void MaybeScheduleCompaction();

    Status GetImpl(LSMKV::Slice key, std::string *val);

    Status WriteImpl(LSMKV::Slice key, LSMKV::Slice val, LSMKV::ValueType type);

public:
    KVStore(const std::string &dir, const std::string &vlog);

    KVStore() = delete;

    ~KVStore() override;

    Status put(LSMKV::Slice key, LSMKV::Slice s) override;

    Status get(LSMKV::Slice key, std::string *val) override;

    std::string get(LSMKV::Slice key) override;

    Status del(LSMKV::Slice key) override;

    Status scan(LSMKV::Slice start, LSMKV::Slice end, std::list<std::pair<std::string, std::string>> &result) override;

	Status scan_w_cro(LSMKV::Slice start, LSMKV::Slice end, std::list<std::pair<std::string, std::string>> &result) override;

    uint64_t ApproximateVLogFileSize() const override;

    Status ExpireAt(LSMKV::SequenceNumber seq);

    void reset() override;

    Status gc() override;
};
