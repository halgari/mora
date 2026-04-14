#include "mora/emit/arrangement_emit.h"
#include "mora/rt/arrangement_view.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(ArrangementView, FindsMatchingKey) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0xAA}}, {{0x20, 0xBB}}, {{0x30, 0xCC}}}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x20);
    ASSERT_EQ(rng.count, 1u);
    EXPECT_EQ(rng.rows[1], 0xBBu);
}

TEST(ArrangementView, MissingKeyReturnsEmptyRange) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0xAA}}, {{0x30, 0xCC}}}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x20);
    EXPECT_EQ(rng.count, 0u);
}

TEST(ArrangementView, MultipleMatchesReturnedAsRange) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0x01}}, {{0x10, 0x02}}, {{0x10, 0x03}}, {{0x20, 0x04}}}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x10);
    EXPECT_EQ(rng.count, 3u);
}

TEST(ArrangementView, IndexedByColumnOne) {
    auto bytes = emit::build_u32_arrangement(0,
        {{{0x10, 0xAA}}, {{0x20, 0xBB}}, {{0x30, 0xAA}}}, /*key_column*/ 1);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0xAA);
    EXPECT_EQ(rng.count, 2u);
}

TEST(ArrangementView, EmptyArrangement) {
    auto bytes = emit::build_u32_arrangement(0, {}, 0);
    rt::ArrangementView v(bytes.data(), bytes.size());
    auto rng = v.equal_range_u32(0x10);
    EXPECT_EQ(rng.count, 0u);
}
