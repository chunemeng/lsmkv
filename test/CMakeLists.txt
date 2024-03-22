include(../extern/gtest/googletest.cmake)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(GTEST_LIBS gtest gmock gtest_main pthread)

add_executable(TEST performance_test.cpp)
add_executable(LSMKVC correctness.cc)
add_executable(LSMKVP persistence.cc)
add_executable(Put throughput_test.cpp)
add_executable(Get random_get_test.cpp)
set(TEST_UTIL_SOURCES testutil.cc testutil.h)

add_executable(Bloom bloom_test.cc ${TEST_UTIL_SOURCES} same_timestamp_test.cpp)

target_link_libraries(TEST LSMKV ${GTEST_LIBS})
target_link_libraries(LSMKVC LSMKV ${GTEST_LIBS})
target_link_libraries(LSMKVP LSMKV ${GTEST_LIBS})
target_link_libraries(Get LSMKV ${GTEST_LIBS})
target_link_libraries(Put LSMKV ${GTEST_LIBS})
target_link_libraries(Bloom LSMKV ${GTEST_LIBS})

add_test(NAME Persistence COMMAND LSMKVP)
add_test(NAME Correctness COMMAND LSMKVC)