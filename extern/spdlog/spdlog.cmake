include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.1.tar.gz
        URL_MD5 3a8f758489a1bf21403eabf49e78c3a4

        DOWNLOAD_NO_PROGRESS 1
        INACTIVITY_TIMEOUT 5
        TIMEOUT 30
)

FetchContent_MakeAvailable(spdlog)

include_directories(${spdlog_SOURCE_DIR}/include)
