#include <iostream>
#include <string>
#include <cassert>
#include <random>

#include "test.h"

class PersistenceTest : public Test {


public:
    const uint64_t TEST_MAX = 1024 * 32;
    const uint64_t GC_TRIGGER = 1024;

    void SetUp() override {
        Test::SetUp();
        KVStoreAPI::Open("/home/data", "/home/data/vlog", &store);
        vlog = "/home/data/vlog";
    }

    void loop(std::atomic<bool> &running) {
        std::cout << "Data is ready, start looping and wait to be terminated!" << std::endl;
        std::cout.flush();
        while (running) {
            for (uint64_t i = 0; i <= 1024; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                store->del(Key(TEST_MAX + i));

                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                store->put(Key(TEST_MAX + i), std::string(1024, '.'));

                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                store->put(Key(TEST_MAX + i), std::string(512, 'x'));
            }
        }
    }

    void test() {
        std::cout << "KVStore Persistence Test" << std::endl;
        std::cout << "<<Test Mode>>" << std::endl;
        uint64_t i;
        // Test _data
        for (i = 0; i < TEST_MAX; ++i) {
            if (i % GC_TRIGGER == 0) [[unlikely]] {
                check_gc(16 * MB);
            }
            switch (i & 3) {
                case 0:
                    EXPECT(std::string(i + 1, 't'), store->get(Key(i)));
                    break;
                case 1:
                    EXPECT(std::string(i + 1, 't'), store->get(Key(i)));
                    break;
                case 2:
                    EXPECT(not_found, store->get(Key(i)));
                    break;
                case 3:
                    EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
                    break;
                default:
                    assert(0);
            }
        }

        phase();

        report();
    }
};


TEST_F(PersistenceTest, Persistence) {
    std::cout << "KVStore Persistence Test" << std::endl;
    std::cout << "<<Preparation Mode>>" << std::endl;
    uint64_t i;

    // Clean up
    store->reset();

    // Test multiple key-value pairs
    for (i = 0; i < TEST_MAX; ++i) {
        store->put(Key(i), std::string(i + 1, 's'));
        EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
    }
    phase();

    // Test after all insertions
    for (i = 0; i < TEST_MAX; ++i)
        EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
    phase();

    // Test deletions
    for (i = 0; i < TEST_MAX; i += 2) {
        EXPECT(true, store->del(Key(i)).is_ok());

        if ((i / 2) % GC_TRIGGER == 0) [[unlikely]] {
            check_gc(16 * MB);
        }
    }

    // Prepare _data for Test Mode
    for (i = 0; i < TEST_MAX; ++i) {
        std::string val = store->get(Key(i));
        switch (i & 3) {
            case 0:
                EXPECT(not_found, store->get(Key(i)));
                store->put(Key(i), std::string(i + 1, 't'));
                break;
            case 1:
                EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
                store->put(Key(i), std::string(i + 1, 't'));
                break;
            case 2:
                EXPECT(not_found, store->get(Key(i)));
                break;
            case 3:
                EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
                break;
            default:
                assert(0);
        }

        if (i % GC_TRIGGER == 0) [[unlikely]] {
            check_gc(8 * MB);
        }
    }

    check_gc(32 * MB);

    phase();

    report();

    std::atomic<bool> running(true);
    std::thread worker([this, &running]() {
        this->loop(running);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(1000, 5000);
    int wait_time = dis(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));
    running = false;
    worker.join();

    std::cout << "Killing loop after " << wait_time << " ms." << std::endl;

    this->test();
}


