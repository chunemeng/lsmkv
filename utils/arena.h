#ifndef ARENA_H
#define ARENA_H

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <cassert>

namespace LSMKV {

  class Arena {
  private:
      static constexpr int align = (sizeof(void *) > 8) ? sizeof(void *) : 8;
      constexpr static int ARENA_BLOCK_SIZE = 4096;

      uint64_t waste_{0};
      uint64_t allocs_{0};
      char *alloc_ptr;
      char *alloc_aligned_ptr;
      size_t alloc_bytes_remaining;
      std::vector<char *> pool;

      void allocatePtr() {
          alloc_ptr = allocateNewBlock(ARENA_BLOCK_SIZE);

          auto padding = reinterpret_cast<uintptr_t>(alloc_ptr) & (align - 1);

          alloc_aligned_ptr = alloc_ptr - padding + ARENA_BLOCK_SIZE;

          waste_ += ARENA_BLOCK_SIZE;
          alloc_bytes_remaining = ARENA_BLOCK_SIZE - padding;
      }


      char *allocateFallBack(size_t bytes) {
          if (bytes > (ARENA_BLOCK_SIZE >> 2)) [[unlikely]] {
              char *result = allocateNewBlock(bytes);
              return result;
          }
          allocatePtr();

          char *result = alloc_ptr;
          alloc_ptr += bytes;
          alloc_bytes_remaining -= bytes;
          return result;
      }

      char *allocateAlignFallBack(size_t bytes) {
          if (bytes > (ARENA_BLOCK_SIZE >> 2)) {
              char *result = allocateNewBlock(bytes + align);
              return reinterpret_cast<char *>((reinterpret_cast<uintptr_t>(result) + align - 1) & ~(align - 1));
          }

          allocatePtr();

          alloc_aligned_ptr -= bytes;

          alloc_bytes_remaining -= bytes;
          return alloc_aligned_ptr;
      }

      char *allocateNewBlock(size_t bytes);

  public:
      uint64_t getWaste() const {
          return allocs_;
      }

      u_int64_t getUsed() const {
          return allocs_;
      }

      Arena() : alloc_ptr(nullptr), alloc_aligned_ptr(nullptr), alloc_bytes_remaining(0) {};

      ~Arena() {
          for (auto &i: pool) {
              delete[] i;
          }
      }

      char *allocate(size_t bytes);

      char *allocateAligned(size_t bytes);
  };

  inline char *Arena::allocate(size_t bytes) {
      allocs_ += bytes;
      waste_ -= bytes;

      if (bytes <= alloc_bytes_remaining) {
          char *result = alloc_ptr;
          alloc_ptr += bytes;
          alloc_bytes_remaining -= bytes;
          assert(alloc_aligned_ptr - alloc_ptr == alloc_bytes_remaining);
          return result;
      }
      return allocateFallBack(bytes);
  }

  inline char *Arena::allocateNewBlock(size_t bytes) {
      char *result = new char[bytes];
      pool.push_back(result);
      return result;
  }

  inline char *Arena::allocateAligned(size_t bytes) {
      // alloc_ptr % align
      allocs_ += bytes;
      waste_ -= bytes;
      if (bytes <= alloc_bytes_remaining) {
          alloc_aligned_ptr -= bytes;

          char *result = alloc_aligned_ptr;
          alloc_bytes_remaining -= bytes;
          return result;
      }

      return allocateAlignFallBack(bytes);
  }
}// namespace LSMKV

#endif//ARENA_H
