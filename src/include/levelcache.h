#ifndef LSMKV_SRC_KEYCACHE_H
#define LSMKV_SRC_KEYCACHE_H

#include "utils/utils.h"
#include "memtable.h"
#include "table.h"
#include "version.h"
#include <list>
#include <map>
#include <queue>
#include <ranges>
#include <set>
#include <unordered_set>
#include "utils/status.h"

namespace LSMKV {
    class LevelCache {
        typedef Table::TableIterator TableIterator;
    public:
        LevelCache(const LevelCache &) = delete;

        const LevelCache &operator=(const LevelCache &) = delete;

        explicit LevelCache(std::string db_path, Version *v);

        void LogLevel(uint64_t level, int i);

        // return timestamp_ to update the version
        uint64_t CompactionSST(uint64_t level,
                               uint64_t file_no,
                               uint64_t size,
                               std::vector<uint64_t> old_file_nos[2],
                               std::vector<uint64_t> &need_to_move,
                               std::vector<WriteSlice> &need_to_write);

        template<typename function>
        void FindCompactionNextLevel(uint64_t level, function const &callback);

        void Merge(uint64_t file_no,
                   uint64_t level,
                   uint64_t timestamp,
                   bool isDrop,
                   std::vector<std::unique_ptr<TableIterator>> &wait_to_merge,
                   std::vector<WriteSlice> &need_to_write);

        void reset();

        char *ReserveCache(size_t size, size_t pair_size, const uint64_t &file_no);

        void scan(const Slice &K1, const Slice &K2, std::map<std::string, std::string> &key_map);

        void PushCache(const char *tmp) {
            assert(table_cache != nullptr);
            table_cache->pushCache(tmp);
            auto it = new TableIterator(table_cache);
            cache[0].emplace(it->timestamp(), it);
            table_cache = nullptr;
        }

        status get(const Slice& key, std::string &val);

        std::string get(uint64_t key) {
            char char_key[8];
            EncodeFixed64(char_key, key);
            Slice ke(char_key, 8);
            std::string val;
            get(ke, val);
            return val;
        }

        ~LevelCache();

        [[nodiscard]] bool empty() const;

        status GetOffset(const Slice& key, uint64_t &offset);

    private:
        const std::string db_name_;
        // key is timestamp_
        // No need to level_order, because it also needs to keep key in order
        // otherwise it sames to this one
        // vector index is level, multimap's key is timestamp_
        std::vector<std::multimap<uint64_t, TableIterator *>> cache;
        Table *table_cache = nullptr;

        // the comparator of Iterator
        struct CmpKey {
            bool operator()(const std::multimap<uint64_t, TableIterator *>::iterator &left,
                            const std::multimap<uint64_t, TableIterator *>::iterator &right) {
                return left->second->SmallestKey() < right->second->SmallestKey();
            }
        };
    };
}

#endif //LSMKV_SRC_KEYCACHE_H
