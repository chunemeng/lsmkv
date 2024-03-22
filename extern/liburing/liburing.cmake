include(ExternalProject)

ExternalProject_Add(
        liburing_external
        URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.4.tar.gz
        URL_HASH MD5=b83a3b7430fae444c7843c2a4b91d2f5
        DOWNLOAD_NO_PROGRESS 1
        INACTIVITY_TIMEOUT 5
        TIMEOUT 30
        CONFIGURE_COMMAND ./configure
        BUILD_COMMAND make
        BUILD_IN_SOURCE 1
        BUILD_ALWAYS false
        BUILD_BYPRODUCTS "<SOURCE_DIR>/src/liburing.a"
        INSTALL_COMMAND ""
        TEST_COMMAND ""
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_MERGED_STDOUTERR ON
        LOG_OUTPUT_ON_FAILURE ON
)

ExternalProject_Get_Property(liburing_external SOURCE_DIR)

add_library(liburing INTERFACE)

add_dependencies(liburing liburing_external)

message(STATUS "liburing source dir: ${SOURCE_DIR}")

target_include_directories(liburing INTERFACE
        ${SOURCE_DIR}/src/include
)

include_directories(${SOURCE_DIR}/src/include)

target_link_libraries(liburing INTERFACE
        "${SOURCE_DIR}/src/liburing.a"
)