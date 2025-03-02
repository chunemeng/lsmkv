#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

namespace LSMKV {
  template<typename K, typename V>
  class Lru_Cache {

  public:
      Lru_Cache() = delete;

      Lru_Cache(uint32_t capacity) : capacity_(capacity) {

      }

      uint32_t Size() const {
          return cache_.size();
      }



      template<typename KK, typename VV>
      void Put(KK &&key, VV &&value) {
          auto it = cache_.find(key);

          if (it != cache_.end()) {
              lru_list_.erase(it->second);
              cache_.erase(it);
          }

          if (cache_.size() == capacity_) {
              auto last = lru_list_.back();
              cache_.erase(last.first);
              lru_list_.pop_back();
          }

          lru_list_.push_front(std::make_pair(std::forward<KK>(key), std::forward<VV>(value)));
          cache_[key] = lru_list_.begin();
      }

      V *Get(const K &key) {
          auto it = cache_.find(key);
          if (it == cache_.end()) {
              return nullptr;
          }

          auto pair = std::make_pair(std::move(it->first), std::move(it->second->second));

          lru_list_.erase(it->second);
          lru_list_.push_front(std::move(pair));
          it->second = lru_list_.begin();
          return &it->second->second;
      }

  private:
      std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> cache_;
      std::list<std::pair<K, V>> lru_list_;
      uint32_t capacity_;

  };
} // namespace LSMKV


