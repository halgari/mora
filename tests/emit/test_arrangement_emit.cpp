#include "mora/emit/arrangement_emit.h"
#include "mora/emit/arrangement.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora::emit;

TEST(ArrangementEmit, TwoColumnU32SortedByFirst) {
    std::vector<std::array<uint32_t, 2>> rows = {
        {0x30, 0x10},
        {0x10, 0x20},
        {0x20, 0x30},
    };
    auto bytes = build_u32_arrangement(/*relation_id*/ 0, rows, /*key_column*/ 0);

    ArrangementHeader h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.row_count, 3u);
    EXPECT_EQ(h.row_stride_bytes, 8u);
    EXPECT_EQ(h.key_column_index, 0u);

    const uint32_t* r = reinterpret_cast<const uint32_t*>(bytes.data() + sizeof(h));
    EXPECT_EQ(r[0], 0x10u); EXPECT_EQ(r[1], 0x20u);
    EXPECT_EQ(r[2], 0x20u); EXPECT_EQ(r[3], 0x30u);
    EXPECT_EQ(r[4], 0x30u); EXPECT_EQ(r[5], 0x10u);
}

TEST(ArrangementEmit, IndexedByDifferentKeyColumn) {
    std::vector<std::array<uint32_t, 2>> rows = {
        {0x30, 0x10},
        {0x10, 0x20},
        {0x20, 0x30},
    };
    auto bytes = build_u32_arrangement(0, rows, /*key_column*/ 1);
    ArrangementHeader h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.key_column_index, 1u);
    const uint32_t* r = reinterpret_cast<const uint32_t*>(bytes.data() + sizeof(h));
    EXPECT_EQ(r[1], 0x10u);
    EXPECT_EQ(r[3], 0x20u);
    EXPECT_EQ(r[5], 0x30u);
}

TEST(ArrangementEmit, EmptyRowsProducesHeaderOnly) {
    auto bytes = build_u32_arrangement(0, {}, 0);
    EXPECT_EQ(bytes.size(), sizeof(ArrangementHeader));
    ArrangementHeader h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.row_count, 0u);
}
