#include "mora/sema/keyword_intern.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(KeywordIntern, FirstKeywordGetsZero) {
    KeywordInterner ki;
    EXPECT_EQ(ki.intern("high"), 0u);
}

TEST(KeywordIntern, SameKeywordSameId) {
    KeywordInterner ki;
    auto a = ki.intern("fire");
    auto b = ki.intern("fire");
    EXPECT_EQ(a, b);
}

TEST(KeywordIntern, DifferentKeywordsDifferentIds) {
    KeywordInterner ki;
    auto a = ki.intern("low");
    auto b = ki.intern("high");
    EXPECT_NE(a, b);
}

TEST(KeywordIntern, Lookup) {
    KeywordInterner ki;
    auto id = ki.intern("ember");
    EXPECT_EQ(ki.name(id), "ember");
}
