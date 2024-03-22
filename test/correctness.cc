#include <iostream>
#include <string>
#include <cassert>

#include "test.h"

class CorrectnessTest : public Test {
public:
    virtual ~CorrectnessTest() = default;

    const uint64_t SIMPLE_TEST_MAX = 512;
    const uint64_t LARGE_TEST_MAX = 1024 * 64;
    const uint64_t GC_TEST_MAX = 1024 * 48;

    void regular_test(uint64_t max) {
        uint64_t i;
        std::string val;


        // Test a single key
        EXPECT(not_found, store->get(Key(1)));
        store->put(Key(1), "SE");
        EXPECT("SE", store->get(Key(1)));
        EXPECT(true, store->del(Key(1)).ok());
        EXPECT(not_found, store->get(Key(1)));
        EXPECT(false, store->del(Key(1)).ok());
        // p1
        phase();

        // Test multiple key-value pairs
        for (i = 0; i < max; ++i) {
            store->put(Key(i), std::string(i + 1, 's'));
            EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
        }
        // p2
        phase();

        // Test after all insertions
        for (i = 0; i < max; ++i)
            EXPECT(std::string(i + 1, 's'), store->get(Key(i)));
        // p3
        phase();

//        Test scan
        std::list<std::pair<std::string, std::string>> list_ans;
        std::list<std::pair<std::string, std::string>> list_stu;

        for (i = 0; i < max / 2; ++i) {
            list_ans.emplace_back(Key(i), std::string(i + 1, 's'));
        }

        store->scan(Key(0), Key(max / 2 - 1), list_stu);
        EXPECT(list_ans.size(), list_stu.size());

        auto ap = list_ans.begin();
        auto sp = list_stu.begin();
        while (ap != list_ans.end()) {
            if (sp == list_stu.end()) {
                EXPECT((*ap).first, Key(-1));
                EXPECT((*ap).second, not_found);
                ap++;
            } else {
                EXPECT((*ap).first, (*sp).first);
                EXPECT((*ap).second, (*sp).second);
                ap++;
                sp++;
            }
        }
        list_stu.clear();
        list_ans.clear();
        // p4
        phase();

        // Test deletions
        for (i = 0; i < max; i += 2) {
            EXPECT(true, store->del(Key(i)).ok());

            if (!(not_found == store->get(Key(i)))) {
                assert(0);
            }
        }

        for (i = 0; i < max; i++) {
            auto g = store->get(Key(i));
            EXPECT((i & 1) ? std::string(i + 1, 's') : not_found,
                   g);
        }
        for (i = 1; i < max; ++i) {
            EXPECT(i & 1, store->del(Key(i)).ok());
        }

        // p6
        phase();

        report();
    }

    void gc_test(uint64_t max) {
        uint64_t i;
        uint64_t gc_trigger = 1024;

        for (i = 0; i < max; ++i) {
            store->put(Key(i), std::string(i + 1, 's'));
        }

        for (i = 0; i < max; ++i) {
            auto gp = store->get(Key(i));
            EXPECT(std::string(i + 1, 's'), gp);

            switch (i % 3) {
                case 0:
                    store->put(Key(i), std::string(i + 1, 'e'));
                    break;
                case 1:
                    store->put(Key(i), std::string(i + 1, '2'));
                    break;
                case 2:
                    store->put(Key(i), std::string(i + 1, '3'));
                    break;
                default:
                    assert(0);
            }

            if (i % gc_trigger == 0) [[unlikely]] {
                check_gc(16 * MB);
            }
        }

        phase();

        for (i = 0; i < max; ++i) {
            switch (i % 3) {
                case 0:
                    EXPECT(std::string(i + 1, 'e'), store->get(Key(i)));
                    break;
                case 1:
                    EXPECT(std::string(i + 1, '2'), store->get(Key(i)));
                    break;
                case 2:
                    EXPECT(std::string(i + 1, '3'), store->get(Key(i)));
                    break;
                default:
                    assert(0);
            }
        }

        phase();

        for (i = 1; i < max; i += 2) {
            EXPECT(true, store->del(Key(i)).ok());

            if ((i - 1) % gc_trigger == 0) [[unlikely]] {
                check_gc(8 * MB);
            }
        }

        for (i = 0; i < max; i += 2) {
            switch (i % 3) {
                case 0:
                    EXPECT(std::string(i + 1, 'e'), store->get(Key(i)));
                    break;
                case 1:
                    EXPECT(std::string(i + 1, '2'), store->get(Key(i)));
                    break;
                case 2:
                    EXPECT(std::string(i + 1, '3'), store->get(Key(i)));
                    break;
                default:
                    assert(0);
            }

            store->del(Key(i));

            if (((i - 1) / 2) % gc_trigger == 0) [[unlikely]] {
                check_gc(32 * MB);
            }
        }

        for (i = 0; i < max; ++i) {
            EXPECT(not_found, store->get(Key(i)));
        }

        phase();

        report();
    }

    void SetUp() override {
        Test::SetUp();
        KVStoreAPI::Open("/home/data", "/home/data/vlog", &store);
        vlog = "/home/data/vlog";
    }

};

TEST_F(CorrectnessTest, SimpleTest) {
    store->reset();

    std::cout << "[Simple Test]" << std::endl;

    regular_test(SIMPLE_TEST_MAX);
}

TEST_F(CorrectnessTest, LargeTest) {
    store->reset();

    std::cout << "[Large Test]" << std::endl;

    regular_test(LARGE_TEST_MAX);
}

TEST_F(CorrectnessTest, GCTest) {
    SetUp();

    store->reset();

    std::cout << "[GC Test]" << std::endl;

    gc_test(GC_TEST_MAX);
}
