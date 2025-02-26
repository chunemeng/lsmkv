#include "block_format.h"

namespace LSMKV {

  Iterator *SSTFileMeta::NewIterator(const std::string &db_name, Comparator *cmp) const {
      return new TableIterator(db_name, this, cmp);
  }

} // namespace LSMKV