include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
        crc32c
        URL https://github.com/google/crc32c/archive/refs/tags/1.1.2.tar.gz
        URL_MD5 cc0338e6a60c38cab04a70a2c36cd9f2

        DOWNLOAD_NO_PROGRESS 1
        INACTIVITY_TIMEOUT 5
        TIMEOUT 30
)

set(CRC32C_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "Disable Benchmarks" FORCE)
set(CRC32C_USE_GLOG OFF CACHE BOOL "Disable Glog" FORCE)
FetchContent_MakeAvailable(crc32c)
include_directories(${crc32c_SOURCE_DIR}/include)