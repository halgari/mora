//
// Unit tests for polymorphic static getters declared in
// data/relations/form/shared.yaml — fields that apply across multiple
// record types (WEAP, ARMO, NPC, ALCH, …). Verifies each one works
// against a host record of each type.
//
// Reference FormIDs from vanilla Skyrim.esm:
//   • Iron Sword  0x00012EB7 (WEAP)
//   • Iron Helmet 0x00012E4D (ARMO)
//   • Nazeem      0x000133FB (NPC_)

#include "skyrim_fixture.h"
#include <gtest/gtest.h>
#include <unordered_set>

namespace {

constexpr uint32_t kIronSword  = 0x00012EB7;
constexpr uint32_t kIronHelmet = 0x00012E4D;
constexpr uint32_t kNazeem     = 0x00013BBF;

class SharedGettersTest : public mora::test::SkyrimTest {};

// ── gold_value: polymorphic across WEAP + ARMO via TESValueForm ──────

TEST_F(SharedGettersTest, GoldValueOnWeapon) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("gold_value", kIronSword, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_EQ(v.as_int(), 25);
}

TEST_F(SharedGettersTest, GoldValueOnArmor) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("gold_value", kIronHelmet, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_EQ(v.as_int(), 60);
}

// ── weight: polymorphic across WEAP + ARMO via TESWeightForm ─────────

TEST_F(SharedGettersTest, WeightOnWeapon) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("weight", kIronSword, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Float);
    EXPECT_FLOAT_EQ(static_cast<float>(v.as_float()), 9.0f);
}

TEST_F(SharedGettersTest, WeightOnArmor) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("weight", kIronHelmet, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Float);
    EXPECT_FLOAT_EQ(static_cast<float>(v.as_float()), 5.0f);
}

// ── name: FULL subrecord. Skyrim.esm is a localized plugin so the ESP
//    reader returns the 4-byte string ID (as Int) rather than the
//    resolved string — we verify the fact exists and is non-zero.

TEST_F(SharedGettersTest, NameOnWeapon) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("name", kIronSword, v));
    // Localized plugin: LString → Int string id, non-zero.
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_NE(v.as_int(), 0);
}

TEST_F(SharedGettersTest, NameOnArmor) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("name", kIronHelmet, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_NE(v.as_int(), 0);
}

TEST_F(SharedGettersTest, NameOnNpc) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("name", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_NE(v.as_int(), 0);
}

// ── has_keyword: polymorphic across WEAP, ARMO, NPC_, ALCH, … via
//    BGSKeywordForm + KWDA subrecord. We assert the weapon has at least
//    one keyword (Iron Sword carries WeapTypeSword, VendorItemWeapon,
//    etc.); exact FormIDs of vanilla keywords aren't version-pinned in
//    our fixture so a count assertion is enough for wiring.

TEST_F(SharedGettersTest, HasKeywordOnWeapon) {
    auto kws = list_formids("has_keyword", kIronSword);
    EXPECT_GE(kws.size(), 1u);
}

TEST_F(SharedGettersTest, HasKeywordOnArmor) {
    auto kws = list_formids("has_keyword", kIronHelmet);
    EXPECT_GE(kws.size(), 1u);
}

TEST_F(SharedGettersTest, HasKeywordOnNpc) {
    // Nazeem has no keywords in vanilla Skyrim.esm — scan for any NPC
    // with at least one has_keyword fact to prove NPC keyword extraction
    // works end-to-end.
    auto rel = pool().intern("has_keyword");
    const auto all = db().get_relation(rel);
    size_t npc_kw_count = 0;
    auto npc_rel = pool().intern("npc");
    const auto npcs = db().get_relation(npc_rel);
    std::unordered_set<uint32_t> npc_fids;
    for (const auto& t : npcs) {
        if (!t.empty() && t[0].kind() == mora::Value::Kind::FormID) {
            npc_fids.insert(t[0].as_formid());
        }
    }
    for (const auto& t : all) {
        if (t.size() >= 2 && t[0].kind() == mora::Value::Kind::FormID &&
            npc_fids.count(t[0].as_formid())) {
            ++npc_kw_count;
        }
    }
    EXPECT_GT(npc_kw_count, 0u);
}

} // namespace
