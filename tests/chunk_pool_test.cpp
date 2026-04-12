#include <gtest/gtest.h>
#include "mora/data/chunk_pool.h"

using namespace mora;

TEST(ChunkPoolTest, AcquireAndRelease) {
    ChunkPool pool;
    U32Chunk* c = pool.acquire_u32();
    ASSERT_NE(c, nullptr);

    // Write some data and set count
    c->data[0] = 42;
    c->data[1] = 99;
    c->count = 2;

    pool.release(c);

    // Reacquire — should get the same pointer back with count reset
    U32Chunk* c2 = pool.acquire_u32();
    EXPECT_EQ(c2, c);
    EXPECT_EQ(c2->count, 0u);
    // Data still there (pool does not zero memory)
    EXPECT_EQ(c2->data[0], 42u);
}

TEST(ChunkPoolTest, MultipleChunks) {
    ChunkPool pool;
    U32Chunk* a = pool.acquire_u32();
    U32Chunk* b = pool.acquire_u32();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
}

TEST(ChunkPoolTest, I64Chunks) {
    ChunkPool pool;
    I64Chunk* c = pool.acquire_i64();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->count, 0u);

    c->data[0] = -1234567890LL;
    c->data[1] = 9876543210LL;
    c->count = 2;

    EXPECT_EQ(c->data[0], -1234567890LL);
    EXPECT_EQ(c->data[1], 9876543210LL);
}

TEST(ChunkPoolTest, F64Chunks) {
    ChunkPool pool;
    F64Chunk* c = pool.acquire_f64();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->count, 0u);

    c->data[0] = 3.14159;
    c->data[1] = -2.71828;
    c->count = 2;

    EXPECT_DOUBLE_EQ(c->data[0], 3.14159);
    EXPECT_DOUBLE_EQ(c->data[1], -2.71828);
}

TEST(ChunkPoolTest, SelectionVectorSelectAll) {
    SelectionVector sv;
    sv.select_all(100);

    EXPECT_EQ(sv.count, 100u);
    for (size_t i = 0; i < 100; i++) {
        EXPECT_EQ(sv.indices[i], static_cast<uint16_t>(i));
    }
}
