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

TEST(ArrangementEmit, SectionWithTwoArrangements) {
    auto a1 = build_u32_arrangement(1, {{{0x10,0x20}}}, 0);
    auto a2 = build_u32_arrangement(2, {{{0x30,0x40}}, {{0x10,0x50}}}, 0);
    auto section = build_arrangements_section({a1, a2});

    uint32_t count = 0;
    std::memcpy(&count, section.data(), 4);
    EXPECT_EQ(count, 2u);

    // First entry: u64 length prefix, then the arrangement bytes.
    uint64_t len1 = 0;
    std::memcpy(&len1, section.data() + 4, 8);
    EXPECT_EQ(len1, a1.size());
    EXPECT_EQ(std::memcmp(section.data() + 4 + 8, a1.data(), a1.size()), 0);

    uint64_t len2 = 0;
    std::memcpy(&len2, section.data() + 4 + 8 + a1.size(), 8);
    EXPECT_EQ(len2, a2.size());
}

TEST(ArrangementEmit, EmptySection) {
    auto section = build_arrangements_section({});
    EXPECT_EQ(section.size(), 4u);
    uint32_t count = 0;
    std::memcpy(&count, section.data(), 4);
    EXPECT_EQ(count, 0u);
}
