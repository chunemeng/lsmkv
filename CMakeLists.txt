cmake_minimum_required(VERSION 3.22.1)

project(LSMKV)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "C compiler version: ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "C++ compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

cmake_policy(SET CMP0135 NEW)

OPTION(ENABLE_TEST "Enable test" ON)
OPTION(ENABLE_DEBUG "Enable debug" OFF)
OPTION(ENABLE_DEBUG_INFO "Enable debug info" ON)
# Sanitizers(enable once at a time)
OPTION(ENABLE_ASAN "Enable AddressSanitizer" OFF)
OPTION(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
OPTION(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if (ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif ()

if (ENABLE_UBSAN)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif ()

if (ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif ()

if (UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif ()

if (ENABLE_DEBUG)
    message(STATUS "Debug mode enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -w")
else ()
    message(STATUS "Release mode enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=native")
endif ()

if (ENABLE_DEBUG_INFO)
    message(STATUS "Debug info enabled")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif ()

include(extern/liburing/liburing.cmake)
include(extern/spdlog/spdlog.cmake)
include(extern/crc32/crc32.cmake)

include_directories("${CMAKE_SOURCE_DIR}")
include_directories("${CMAKE_SOURCE_DIR}/include")

add_subdirectory(src)

if (ENABLE_TEST)
    enable_testing()
    add_subdirectory(test)
endif ()








