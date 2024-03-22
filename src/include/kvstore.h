#pragma once

#include <future>
#include <mutex>
#include <utility>

#include "builder.h"
#include "cache.h"
#include "levelcache.h"
#include "memtable.h"
#include "performance.h"
#include "version.h"
#include "LSMKV/kvstore_api.h"
#include "utils/executor.h"
#include "db_info.h"
#include "logger.h"
#include "vlogbuilder.h"

class KVStore : public KVStoreAPI {
private:
    LSMKV::Executor<> scheduler_;
    LSMKV::Version *version_;

    LSMKV::DB_Info db_info;
    std::optional<std::future<void>> future_;
    LSMKV::Builder *builder_;
    LSMKV::LevelCache *kc;
    LSMKV::Cache *cache;

    LSMKV::VLogBuilder *vlog_;
    LSMKV::log::Writer *log_;

    LSMKV::WritableFile *log_file_;

    static constexpr int MEM_MAX_SIZE = LSMKV::Option::mem_max_size_;


    static constexpr bool enable_crc_check = true;


    std::unique_ptr<LSMKV::MemTable> mem;

    std::unique_ptr<LSMKV::MemTable> imm;

    void genBuilder();

    void putWhenGc(key_t key, const LSMKV::Slice &s);

    bool GetOffset(key_t key, uint64_t &offset);

    int writeLevel0Table(LSMKV::MemTable *memTable);

    const std::string &DBName() const {
        return db_info.dbname;
    }

    const std::string &VLogPath() const {
        return db_info.vlog;
    }

    status prepare();


public:
    KVStore(const std::string &dir, const std::string &vlog);

    KVStore() = delete;

    ~KVStore();

    status put(LSMKV::Slice key, LSMKV::Slice s) override;

    status get(LSMKV::Slice key, std::string *val) override;

    std::string get(LSMKV::Slice key) override;

    status del(key_t key) override;

    status scan(key_t start, key_t end, std::list<std::pair<std::string, std::string>> &result) override;

    void reset() override;

    void gc(uint64_t chunk_size) override;
};
