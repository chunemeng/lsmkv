#include "utils/comparator.h"
#include "gtest/gtest.h"

using namespace LSMKV;
class ComparatorTest : public ::testing::Test {
public:
    ComparatorTest() {}

    ~ComparatorTest() {}

};

TEST_F(ComparatorTest, BasicTest) {
    struct AAComparator : public UserDefinedComparator {
        int compare_impl(const Slice &a, const Slice &b) const override {
            return 0;
        }

        void find_shortest_separator_impl(std::string *start, const Slice &limit) const override {
        }

        void find_short_successor_impl(std::string *key) const override {
        }

        const char *name_impl() const override {
            return "AAComparator";
        }
    };
    std::unique_ptr<UserDefinedComparator> comparator1 = std::make_unique<AAComparator>();

    EXPECT_EQ(comparator1->name(), std::string{"AAComparator"});
    Comparator comparator2(comparator1.get());
    EXPECT_EQ(comparator2.name(), std::string{"AAComparator"});



    Comparator comparator3{UserKeyComparator{}};

    EXPECT_EQ(comparator3.name(), std::string{"UserKeyComparator"});

}
