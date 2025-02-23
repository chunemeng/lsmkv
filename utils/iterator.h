#ifndef LSM_ITERATOR_H
#define LSM_ITERATOR_H

#include <cstdint>
#include <list>
#include "slice.h"

namespace LSMKV {
  class Iterator {
  public:
      Iterator() = default;

      Iterator(const Iterator &) = delete;

      Iterator &operator=(const Iterator &) = delete;

      virtual ~Iterator() = 0;

      virtual bool valid() const = 0;

      virtual void seekToFirst() = 0;

      virtual void seek(const Slice &target) = 0;

      virtual void next() = 0;

      virtual void seek(const Slice &K1,
                        const Slice &K2) = 0;

      virtual Slice key() const = 0;

      virtual Slice value() const = 0;
  };

  inline Iterator::~Iterator() = default;

}

#endif //LSM_ITERATOR_H
