include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
        concurrentqueue
        URL https://github.com/cameron314/concurrentqueue/archive/refs/tags/v1.0.4.tar.gz
        URL_MD5 5a07b19398f578e8626ca48764b336b1

        DOWNLOAD_NO_PROGRESS 1
        INACTIVITY_TIMEOUT 5
        TIMEOUT 30
)

FetchContent_MakeAvailable(concurrentqueue)

include_directories(${concurrentqueue_SOURCE_DIR})