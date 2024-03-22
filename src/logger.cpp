#include "include/logger.h"

LSMKV::log::Writer::Writer(LSMKV::WritableFile *dest) : dest_(dest), block_offset_(0) {
//    InitTypeCrc(type_crc_);
}
