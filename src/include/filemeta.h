
#ifndef FILEMETA_H
#define FILEMETA_H

#include "dbformat.h"
#include <cstdint>

namespace LSMKV {
  struct SSTFileMeta {
      // 8 bytes kv_size
      // 8bytes largest 	8 bytes smallest
      uint64_t file_size_{};
      uint64_t file_number_{};
      uint32_t level_{};

      InternalKey smallest{};
      InternalKey largest{};
  };
}


#endif //FILEMETA_H
