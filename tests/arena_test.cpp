// tests/arena_test.cpp
#include <gtest/gtest.h>
#include "mora/core/arena.h"

struct TestNode {
    int value;
    TestNode* next;
};

TEST(ArenaTest, AllocateSingleObject) {
    mora::Arena arena;
    auto* node = arena.alloc<TestNode>();
    ASSERT_NE(node, nullptr);
    node->value = 42;
    EXPECT_EQ(node->value, 42);
}

TEST(ArenaTest, AllocateMultipleObjects) {
    mora::Arena arena;
    auto* a = arena.alloc<TestNode>();
    auto* b = arena.alloc<TestNode>();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    a->value = 1;
    b->value = 2;
    EXPECT_EQ(a->value, 1);
    EXPECT_EQ(b->value, 2);
}

TEST(ArenaTest, AllocateManyObjectsAcrossChunks) {
    mora::Arena arena(64); // small chunk size to force multiple chunks
    std::vector<TestNode*> nodes;
    for (int i = 0; i < 100; i++) {
        auto* n = arena.alloc<TestNode>();
        ASSERT_NE(n, nullptr);
        n->value = i;
        nodes.push_back(n);
    }
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(nodes[i]->value, i);
    }
}

TEST(ArenaTest, ResetFreesAll) {
    mora::Arena arena;
    arena.alloc<TestNode>();
    arena.alloc<TestNode>();
    size_t before = arena.bytes_allocated();
    EXPECT_GT(before, 0u);
    arena.reset();
    EXPECT_EQ(arena.bytes_allocated(), 0u);
}
