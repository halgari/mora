//
// Unit tests for every static getter declared against LVLI / LVLN records.
//
// Reference FormIDs from vanilla Skyrim.esm:
//   • LItemEnchWeaponBowBlacksmith 0x00000EDE — populated LVLI.
//
// See data/relations/form/leveled_list.yaml.

#include "skyrim_fixture.h"
#include <gtest/gtest.h>

namespace {

constexpr uint32_t kLItemBlacksmithBow = 0x00000EDE;

class LeveledListGettersTest : public mora::test::SkyrimTest {};

// ── Existence predicates (form_model.h kFormTypes) ───────────────────

TEST_F(LeveledListGettersTest, LeveledListExistencePredicate) {
    EXPECT_TRUE(has_fact_for("leveled_list", kLItemBlacksmithBow));
}

TEST_F(LeveledListGettersTest, LeveledCharCountNonZero) {
    // Proves the LVLN existence path registers facts — any non-zero count is
    // sufficient (Skyrim.esm ships hundreds of leveled character lists).
    auto rel = pool().intern("leveled_char");
    EXPECT_GT(db().fact_count(rel), 0u);
}

// ── chance_none: leveled_list.yaml subrecord extract, LVLI/LVLD as uint8.

TEST_F(LeveledListGettersTest, ChanceNoneFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("chance_none", kLItemBlacksmithBow, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_GE(v.as_int(), 0);
    EXPECT_LE(v.as_int(), 100);
}

// ── leveled_entry: list_field extract from LVLO @4 as FormID. Exact
//    member FormIDs aren't version-pinned so we assert on cardinality.

TEST_F(LeveledListGettersTest, LeveledEntryList) {
    auto entries = list_formids("leveled_entry", kLItemBlacksmithBow);
    EXPECT_GE(entries.size(), 1u);
    for (uint32_t fid : entries) {
        EXPECT_NE(fid, 0u);
    }
}

} // namespace
