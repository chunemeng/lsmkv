#ifndef CODING_H
#define CODING_H

#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>
#include <cassert>

namespace LSMKV {
// Note: memcpy is needed to avoid alignment issues
  inline uint64_t DecodeFixed64(const char *ptr) {
	  uint64_t value{};
	  memcpy(&value, ptr, sizeof(uint64_t));
	  if constexpr (std::endian::native == std::endian::big) {
		  value = std::byteswap(value);
	  }
	  return value;
  }

  inline uint32_t DecodeFixed32(const char *ptr) {
	  uint32_t value{};
	  memcpy(&value, ptr, sizeof(uint32_t));
	  if constexpr (std::endian::native == std::endian::big) {
		  value = std::byteswap(value);
	  }
	  return value;
  }

  inline uint16_t DecodeFixed16(const char *ptr) {
	  uint16_t value{};
	  memcpy(&value, ptr, sizeof(uint16_t));
	  if constexpr (std::endian::native == std::endian::big) {
		  value = std::byteswap(value);
	  }
	  return value;
  }

  inline uint8_t DecodeFixed8(const char *ptr) {
	  return static_cast<uint8_t>(*ptr);
  }

  inline void EncodeFixed8(char *dst, uint8_t value) {
	  assert(value <= 0x7f);
	  dst[0] = static_cast<char>(value);
  }

  inline void EncodeFixed16(char *dst, uint16_t value) {
	  auto *const buffer = reinterpret_cast<uint8_t *>(dst);
	  if constexpr (std::endian::native == std::endian::big) {
		  value = std::byteswap(value);
	  }
	  memcpy(buffer, &value, sizeof(uint16_t));
  }

  inline void EncodeFixed32(char *dst, uint32_t value) {
	  auto *const buffer = reinterpret_cast<uint8_t *>(dst);
	  if constexpr (std::endian::native == std::endian::big) {
		  value = std::byteswap(value);
	  }
	  memcpy(buffer, &value, sizeof(uint32_t));
  }

  inline void EncodeFixed64(char *dst, uint64_t value) {

	  auto *const buffer = reinterpret_cast<uint8_t *>(dst);
	  if constexpr (std::endian::native == std::endian::big) {
		  value = std::byteswap(value);
	  }
	  memcpy(buffer, &value, sizeof(uint64_t));
  }

  inline void EncodeFixed128(char *dst, uint64_t value1, uint64_t value2) {
	  EncodeFixed64(dst, value1);
	  EncodeFixed64(dst + 8, value2);
  }

  inline std::pair<uint64_t, uint64_t> DecodeFixed128(const char *ptr) {
	  return {DecodeFixed64(ptr), DecodeFixed64(ptr + 8)};
  }

}// namespace LSMKV

#endif//CODING_H
