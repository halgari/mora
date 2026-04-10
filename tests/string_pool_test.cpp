#include <gtest/gtest.h>
#include "mora/core/string_pool.h"

TEST(StringPoolTest, InternReturnsSamePointer) {
    mora::StringPool pool;
    auto a = pool.intern("hello");
    auto b = pool.intern("hello");
    EXPECT_EQ(a, b);
}

TEST(StringPoolTest, DifferentStringsGetDifferentIds) {
    mora::StringPool pool;
    auto a = pool.intern("hello");
    auto b = pool.intern("world");
    EXPECT_NE(a, b);
}

TEST(StringPoolTest, CanRetrieveString) {
    mora::StringPool pool;
    auto id = pool.intern("test_string");
    EXPECT_EQ(pool.get(id), "test_string");
}

TEST(StringPoolTest, EmptyString) {
    mora::StringPool pool;
    auto id = pool.intern("");
    EXPECT_EQ(pool.get(id), "");
}

TEST(StringPoolTest, ManyStrings) {
    mora::StringPool pool;
    std::vector<mora::StringId> ids;
    for (int i = 0; i < 1000; i++) {
        ids.push_back(pool.intern("str_" + std::to_string(i)));
    }
    for (int i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.get(ids[i]), "str_" + std::to_string(i));
    }
    for (int i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.intern("str_" + std::to_string(i)), ids[i]);
    }
}
