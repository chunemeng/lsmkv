
#include <gtest/gtest.h>
#include <include/lsmkv/kvstore_api.h>
#include <src/include/kvstore.h>
#include <chrono>

class SSTModelGetTest : public ::testing::Test {
private:
    const int value_size = 512;

    std::string Key(uint64_t i) {
        std::string s;
        s.resize(8);
        LSMKV::EncodeFixed64(s.data(), std::byteswap(i));
        return s;
    }

    void regular_test(uint64_t max) {

    }

    std::unique_ptr<KVStoreAPI> store;

public:
    SSTModelGetTest() {
        store = std::make_unique<KVStore>("/tmp/lsmkv", "/tmp/lsmkv");
    }

    uint32_t size = 100000;
    uint32_t count{};

    void Init() {
        for (uint32_t i = 0; i < size; i++) {
            std::string value(value_size, 'a');
            store->put(Key(i), value);
        }
    }

    void Run() {
        for (int i = 0; i < 100; ++i) {
            for (int j = 0; j < size / 10; ++j) {
                auto key = Key(j);
                auto value = store->get(key);
                count += value.size();
            }
        }
    }
};

TEST_F(SSTModelGetTest, GetTest) {
    Init();
    std::chrono::high_resolution_clock::time_point start, end;
    start = std::chrono::high_resolution_clock::now();
    Run();
    end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double count = size * 10;
    double throughput = (count * 1e9) / duration;
    std::cout << "throughput: " << throughput << " ops/sec" << std::endl;
};