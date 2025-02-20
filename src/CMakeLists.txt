
add_library(LSMKV STATIC kvstore.cc)

add_dependencies(LSMKV liburing)

file(GLOB_RECURSE SRC_FILES "*.cpp")

target_sources(LSMKV PRIVATE ${SRC_FILES})

target_include_directories(LSMKV PRIVATE
        ${CMAKE_SOURCE_DIR}/utils
        ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(LSMKV PRIVATE
        liburing
)

target_link_libraries(LSMKV PRIVATE
        spdlog
)
