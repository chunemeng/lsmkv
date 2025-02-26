#pragma once

#include "utils/status.h"
#include <cstdint>
#include <list>
#include <string>
#include "utils/slice.h"
#include "utils/coding.h"
#include "utils/utils.h"

class KVStoreAPI {
public:
    using Status = LSMKV::Status;

    static Status Open(const std::string &dir, const std::string &vlog, KVStoreAPI **ptr);

    static Status Open(const std::string &dir, const std::string &vlog, std::unique_ptr<KVStoreAPI> *ptr);

    KVStoreAPI() = default;

    KVStoreAPI(const KVStoreAPI &) = delete;

    KVStoreAPI &operator=(const KVStoreAPI &) = delete;

    KVStoreAPI(KVStoreAPI &&) = delete;

    virtual ~KVStoreAPI() = default;

    /**
     * Insert/Update the key-value pair.
     * No return values for simplicity.
     */
    virtual Status put(LSMKV::Slice key, LSMKV::Slice val) = 0;

    virtual Status get(LSMKV::Slice key, std::string *val) = 0;

    virtual std::string get(LSMKV::Slice key) = 0;

    virtual uint64_t ApproximateVLogFileSize() const = 0;

    /**
     * Delete the given key-value pair if it exists.
     * Returns false iff the key is not found.
     */
    virtual Status del(LSMKV::Slice key) = 0;

    /**
     * This resets the kvstore. All key-value pairs should be removed,
     * including memtable and all sstables files.
     */
    virtual void reset() = 0;

    /**
     * Return a list including all the key-value pair between key1 and key2.
     * keys in the list should be in an ascending order.
     * An empty string indicates not found.
     */
    virtual Status scan(LSMKV::Slice key1, LSMKV::Slice key2, std::list<std::pair<std::string, std::string>> &list) = 0;

    /**
     * This reclaims space from vLog by moving valid value and discarding invalid value.
     * chunk_size is the _size in byte you should AT LEAST recycle.
     */
    virtual void gc(uint64_t chunk_size) = 0;
};
