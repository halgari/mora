#include <gtest/gtest.h>
#include "mora/eval/pipeline.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/chunk_pool.h"

using namespace mora;

// ---------------------------------------------------------------------------
// 1. ScanAllRows — 5000-row 2-column (U32, U32) relation; total sel.count == 5000
// ---------------------------------------------------------------------------
TEST(PipelineTest, ScanAllRows) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::U32}, pool);

    for (uint32_t i = 0; i < 5000; i++) {
        rel.append_row({static_cast<uint64_t>(i), static_cast<uint64_t>(i + 1)});
    }

    size_t total = 0;
    scan(rel, [&](const DataChunk& dc) {
        total += dc.sel.count;
    });

    EXPECT_EQ(total, 5000u);
}

// ---------------------------------------------------------------------------
// 2. ScanWithFilter — 3 rows with col0=100, 2 rows with col0=200; filter on 100
// ---------------------------------------------------------------------------
TEST(PipelineTest, ScanWithFilter) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::U32}, pool);

    rel.append_row({100u, 1u});
    rel.append_row({200u, 2u});
    rel.append_row({100u, 3u});
    rel.append_row({200u, 4u});
    rel.append_row({100u, 5u});

    size_t matched = 0;
    scan_filtered(rel, 0, 100u, [&](const DataChunk& dc) {
        matched += dc.sel.count;
    });

    EXPECT_EQ(matched, 3u);
}

// ---------------------------------------------------------------------------
// 3. ScanEmpty — callback must never be called for an empty relation
// ---------------------------------------------------------------------------
TEST(PipelineTest, ScanEmpty) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::U32}, pool);

    int calls = 0;
    scan(rel, [&](const DataChunk&) { ++calls; });

    EXPECT_EQ(calls, 0);
}

// ---------------------------------------------------------------------------
// 4. ScanFilterNoMatch — filter value absent; callback must never fire
// ---------------------------------------------------------------------------
TEST(PipelineTest, ScanFilterNoMatch) {
    ChunkPool pool;
    ColumnarRelation rel(1, {ColType::U32}, pool);

    for (uint32_t i = 0; i < 10; i++) {
        rel.append_row({static_cast<uint64_t>(i)});
    }

    int calls = 0;
    scan_filtered(rel, 0, 999u, [&](const DataChunk&) { ++calls; });

    EXPECT_EQ(calls, 0);
}

// ---------------------------------------------------------------------------
// 5. DataChunkColumnAccess — verify dc.u32(0)->data[sel.indices[0]] is correct
// ---------------------------------------------------------------------------
TEST(PipelineTest, DataChunkColumnAccess) {
    ChunkPool pool;
    ColumnarRelation rel(2, {ColType::U32, ColType::U32}, pool);

    // Row 0: col0=42, col1=99
    rel.append_row({42u, 99u});
    rel.append_row({7u,  8u});

    bool visited = false;
    scan(rel, [&](const DataChunk& dc) {
        ASSERT_GE(dc.sel.count, 1u);
        uint16_t first_idx = dc.sel.indices[0];
        EXPECT_EQ(dc.u32(0)->data[first_idx], 42u);
        EXPECT_EQ(dc.u32(1)->data[first_idx], 99u);
        visited = true;
    });

    EXPECT_TRUE(visited);
}

// ---------------------------------------------------------------------------
// 6. MultiChunkScan — 5000 rows span 3 chunks; callback fires exactly 3 times
// ---------------------------------------------------------------------------
TEST(PipelineTest, MultiChunkScan) {
    ChunkPool pool;
    ColumnarRelation rel(1, {ColType::U32}, pool);

    for (uint32_t i = 0; i < 5000; i++) {
        rel.append_row({static_cast<uint64_t>(i)});
    }

    ASSERT_EQ(rel.chunk_count(), 3u);

    int calls = 0;
    scan(rel, [&](const DataChunk&) { ++calls; });

    EXPECT_EQ(calls, 3);
}
