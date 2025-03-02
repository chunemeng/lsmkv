include(../extern/gtest/googletest.cmake)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(GTEST_LIBS gtest gmock gtest_main pthread)

add_executable(TEST performance_test.cpp)
add_executable(LSMKVC correctness.cpp)
add_executable(LSMKVP persistence.cpp)
add_executable(Put throughput_test.cpp)
add_executable(Get random_get_test.cpp)
add_executable(Comparator comparator_test.cpp)
set(TEST_UTIL_SOURCES testutil.cpp testutil.h)

file(GLOB TEST_SOURCES "*.cpp")

foreach (TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${TEST_UTIL_SOURCES})
    target_link_libraries(${TEST_NAME} LSMKV ${GTEST_LIBS})
endforeach ()
