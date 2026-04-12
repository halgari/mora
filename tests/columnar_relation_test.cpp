#include <gtest/gtest.h>
#include "mora/data/columnar_relation.h"
#include <cstring>

using namespace mora;

// Helper: pack a double into uint64_t for append_row
static uint64_t f64_bits(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// 1. CreateAndAddRows
TEST(ColumnarRelationTest, CreateAndAddRows) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::U32}, pool);

    EXPECT_EQ(rel.row_count(), 0u);
    EXPECT_EQ(rel.arity(), 2u);

    rel.append_row({10u, 20u});
    rel.append_row({30u, 40u});
    rel.append_row({50u, 60u});

    EXPECT_EQ(rel.row_count(), 3u);
    EXPECT_EQ(rel.chunk_count(), 1u);

    const U32Chunk* c0 = rel.u32_chunk(0, 0);
    const U32Chunk* c1 = rel.u32_chunk(1, 0);
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c0->data[0], 10u);
    EXPECT_EQ(c0->data[1], 30u);
    EXPECT_EQ(c0->data[2], 50u);
    EXPECT_EQ(c1->data[0], 20u);
    EXPECT_EQ(c1->data[1], 40u);
    EXPECT_EQ(c1->data[2], 60u);
}

// 2. ScanColumn
TEST(ColumnarRelationTest, ScanColumn) {
    ChunkPool pool;
    ColumnarRelation rel(1, {ColType::U32}, pool);

    for (uint32_t i = 0; i < 5000; ++i) {
        rel.append_row({static_cast<uint64_t>(i)});
    }

    EXPECT_EQ(rel.row_count(), 5000u);
    // ceil(5000 / 2048) = 3
    EXPECT_EQ(rel.chunk_count(), 3u);

    const U32Chunk* first = rel.u32_chunk(0, 0);
    EXPECT_EQ(first->count, CHUNK_SIZE);  // 2048

    const U32Chunk* last = rel.u32_chunk(0, 2);
    EXPECT_EQ(last->count, 5000u - 2u * CHUNK_SIZE);  // 904

    // Spot-check values: row i should have value i
    // First chunk: rows 0..2047
    EXPECT_EQ(first->data[0], 0u);
    EXPECT_EQ(first->data[2047], 2047u);

    // Last chunk: rows 4096..4999
    EXPECT_EQ(last->data[0], 4096u);
    EXPECT_EQ(last->data[903], 4999u);
}

// 3. BuildHashIndex
TEST(ColumnarRelationTest, BuildHashIndex) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::U32}, pool);

    // Key 1 appears 3 times, key 2 appears 2 times, key 3 once
    rel.append_row({1u, 100u});
    rel.append_row({2u, 200u});
    rel.append_row({1u, 101u});
    rel.append_row({3u, 300u});
    rel.append_row({2u, 201u});
    rel.append_row({1u, 102u});

    rel.build_index(0);

    const auto& refs1 = rel.lookup(0, 1u);
    EXPECT_EQ(refs1.size(), 3u);

    const auto& refs2 = rel.lookup(0, 2u);
    EXPECT_EQ(refs2.size(), 2u);

    const auto& refs3 = rel.lookup(0, 3u);
    EXPECT_EQ(refs3.size(), 1u);

    // Verify one RowRef is correct: first occurrence of key=1 is row 0, chunk 0
    EXPECT_EQ(refs1[0].chunk_idx, 0u);
    EXPECT_EQ(refs1[0].row_idx, 0u);
}

// 4. EmptyRelation
TEST(ColumnarRelationTest, EmptyRelation) {
    ChunkPool pool;
    ColumnarRelation rel(3, {ColType::U32, ColType::I64, ColType::F64}, pool);

    EXPECT_EQ(rel.row_count(), 0u);
    EXPECT_EQ(rel.chunk_count(), 0u);
    EXPECT_EQ(rel.arity(), 3u);

    // lookup on an empty relation returns empty
    const auto& refs = rel.lookup(0, 42u);
    EXPECT_TRUE(refs.empty());
}

// 5. I64Column
TEST(ColumnarRelationTest, I64Column) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::I64}, pool);

    // negative int64 values
    int64_t neg1 = -1234567890LL;
    int64_t neg2 = -9876543210LL;

    rel.append_row({1u, static_cast<uint64_t>(neg1)});
    rel.append_row({2u, static_cast<uint64_t>(neg2)});

    EXPECT_EQ(rel.row_count(), 2u);

    const I64Chunk* c = rel.i64_chunk(1, 0);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->data[0], neg1);
    EXPECT_EQ(c->data[1], neg2);
    EXPECT_EQ(c->count, 2u);

    // Col 0 should still be correct
    const U32Chunk* c0 = rel.u32_chunk(0, 0);
    EXPECT_EQ(c0->data[0], 1u);
    EXPECT_EQ(c0->data[1], 2u);
}

// 6. F64Column
TEST(ColumnarRelationTest, F64Column) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::F64}, pool);

    double pi = 3.14159265358979;
    double e  = 2.71828182845905;

    rel.append_row({7u, f64_bits(pi)});
    rel.append_row({8u, f64_bits(e)});

    EXPECT_EQ(rel.row_count(), 2u);

    const F64Chunk* c = rel.f64_chunk(1, 0);
    ASSERT_NE(c, nullptr);
    EXPECT_DOUBLE_EQ(c->data[0], pi);
    EXPECT_DOUBLE_EQ(c->data[1], e);
    EXPECT_EQ(c->count, 2u);
}

// 7. LookupMiss
TEST(ColumnarRelationTest, LookupMiss) {
    ChunkPool pool;
    ColumnarRelation rel(1, {ColType::U32}, pool);

    rel.append_row({10u});
    rel.append_row({20u});
    rel.build_index(0);

    // Value 99 was never inserted
    const auto& refs = rel.lookup(0, 99u);
    EXPECT_TRUE(refs.empty());
}

// 8. ExactChunkBoundary
TEST(ColumnarRelationTest, ExactChunkBoundary) {
    ChunkPool pool;
    ColumnarRelation rel(1, {ColType::U32}, pool);

    // Fill exactly one chunk
    for (size_t i = 0; i < CHUNK_SIZE; ++i) {
        rel.append_row({static_cast<uint64_t>(i)});
    }

    EXPECT_EQ(rel.row_count(), CHUNK_SIZE);
    EXPECT_EQ(rel.chunk_count(), 1u);

    // Verify the chunk is full
    const U32Chunk* c0 = rel.u32_chunk(0, 0);
    EXPECT_EQ(c0->count, CHUNK_SIZE);
    EXPECT_EQ(c0->data[CHUNK_SIZE - 1], static_cast<uint32_t>(CHUNK_SIZE - 1));

    // Add one more row — should spill into a second chunk
    rel.append_row({9999u});

    EXPECT_EQ(rel.row_count(), CHUNK_SIZE + 1);
    EXPECT_EQ(rel.chunk_count(), 2u);

    const U32Chunk* c1 = rel.u32_chunk(0, 1);
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c1->count, 1u);
    EXPECT_EQ(c1->data[0], 9999u);
}
