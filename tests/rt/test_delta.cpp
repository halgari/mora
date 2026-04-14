#include "mora/rt/delta.h"
#include <gtest/gtest.h>

using namespace mora::rt;

TEST(Delta, PositiveAndNegative) {
    Delta d{.tuple = {1, 2, 3}, .diff = +1};
    EXPECT_EQ(d.diff, 1);
    EXPECT_EQ(d.tuple.size(), 3u);
    Delta r{.tuple = {1, 2, 3}, .diff = -1};
    EXPECT_EQ(r.diff, -1);
}

TEST(DeltaQueue, FifoPushPop) {
    DeltaQueue q;
    q.push(0, {.tuple = {1}, .diff = +1});
    q.push(0, {.tuple = {2}, .diff = +1});
    ASSERT_EQ(q.size(), 2u);
    auto [node, d] = q.pop();
    EXPECT_EQ(node, 0u);
    EXPECT_EQ(d.tuple[0], 1u);
    EXPECT_EQ(q.size(), 1u);
}

TEST(DeltaQueue, EmptyFlag) {
    DeltaQueue q;
    EXPECT_TRUE(q.empty());
    q.push(5, {.tuple={0u}, .diff=+1});
    EXPECT_FALSE(q.empty());
}
