//
// Unit tests for every static getter declared against NPC_ records.
//
// Reference FormID from vanilla Skyrim.esm:
//   • Nazeem 0x00013BBF — canonical test target (used by existing
//     integration test tests/integration/nazeem/).
//
// Bit predicates (essential, protected, unique, female) and list fields
// (spell, perk, inventory_item) are asserted via aggregate counts across
// Skyrim.esm rather than pinned to Nazeem, because individual NPCs'
// flag bits drift across patches while the aggregate population is
// stable for schema-wiring purposes.
//
// See data/relations/form/npc.yaml and include/mora/data/form_model.h.

#include "skyrim_fixture.h"
#include <gtest/gtest.h>

namespace {

constexpr uint32_t kNazeem = 0x00013BBF;

class NpcGettersTest : public mora::test::SkyrimTest {};

// ── Existence predicate ──────────────────────────────────────────────

TEST_F(NpcGettersTest, NpcExistencePredicate) {
    EXPECT_TRUE(has_fact_for("npc", kNazeem));
}

// ── ACBS packed fields: base_level, calc_level_min/max, speed_mult ───
//
// NOTE: `base_level` is not currently extracted. form_model.h:452 has
// `{13, "NPC_", "ACBS", 8, ...}` which uses field_idx=13 (calc_level_min)
// with offset 8 (base_level's location) — a genuine form_model.h bug.
// The net effect is: `base_level` is never registered, and `calc_level_min`
// is registered reading ACBS@8 (which is actually base_level's bytes).
// The YAML `base_level` entry is declarative-only (no `extract:`) so it
// doesn't fill the gap. We assert the current (broken) state so a future
// form_model.h fix causes a loud re-calibration here.

TEST_F(NpcGettersTest, BaseLevelNotCurrentlyExtracted) {
    EXPECT_EQ(db().fact_count(pool().intern("base_level")), 0u);
}

TEST_F(NpcGettersTest, NazeemCalcLevelMinFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("calc_level_min", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
}

TEST_F(NpcGettersTest, NazeemCalcLevelMaxFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("calc_level_max", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
}

TEST_F(NpcGettersTest, NazeemSpeedMult) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("speed_mult", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    // Vanilla default is 100 (normal movement speed).
    EXPECT_EQ(v.as_int(), 100);
}

// ── Const FormRef fields via form_model.h (race_of) + YAML (npc_class,
//    voice_type, default_outfit) ────────────────────────────────────

TEST_F(NpcGettersTest, NazeemRaceFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("race_of", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::FormID);
    EXPECT_NE(v.as_formid(), 0u);
}

TEST_F(NpcGettersTest, NazeemClassFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("npc_class", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::FormID);
    EXPECT_NE(v.as_formid(), 0u);
}

TEST_F(NpcGettersTest, NazeemVoiceTypeFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("voice_type", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::FormID);
    EXPECT_NE(v.as_formid(), 0u);
}

TEST_F(NpcGettersTest, NazeemDefaultOutfitFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("default_outfit", kNazeem, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::FormID);
    EXPECT_NE(v.as_formid(), 0u);
}

// ── ACBS bit predicates: asserted on aggregate population because
//    individual NPCs' flags drift across patches. Each predicate should
//    fire for many NPCs and never for all of them.

TEST_F(NpcGettersTest, FemalePredicatePopulated) {
    auto n_female = db().fact_count(pool().intern("female"));
    auto n_npc    = db().fact_count(pool().intern("npc"));
    EXPECT_GT(n_female, 0u);
    EXPECT_LT(n_female, n_npc);
}

TEST_F(NpcGettersTest, EssentialPredicatePopulated) {
    auto n = db().fact_count(pool().intern("essential"));
    auto total = db().fact_count(pool().intern("npc"));
    EXPECT_GT(n, 0u);
    EXPECT_LT(n, total);
}

TEST_F(NpcGettersTest, UniquePredicatePopulated) {
    auto n = db().fact_count(pool().intern("unique"));
    auto total = db().fact_count(pool().intern("npc"));
    EXPECT_GT(n, 0u);
    EXPECT_LT(n, total);
}

TEST_F(NpcGettersTest, ProtectedPredicatePopulated) {
    auto n = db().fact_count(pool().intern("protected"));
    auto total = db().fact_count(pool().intern("npc"));
    EXPECT_GT(n, 0u);
    EXPECT_LT(n, total);
}

// ── Collections: repeating subrecords (SPLO / PRKR / CNTO) ──────────

TEST_F(NpcGettersTest, SpellListExtractsSomeFacts) {
    // Many Skyrim.esm NPCs have at least one SPLO (racial spells, ability
    // spells). Schema wiring is validated by any non-zero population.
    EXPECT_GT(db().fact_count(pool().intern("spell")), 0u);
}

TEST_F(NpcGettersTest, PerkListExtractsSomeFacts) {
    EXPECT_GT(db().fact_count(pool().intern("perk")), 0u);
}

TEST_F(NpcGettersTest, InventoryItemExtractsSomeFacts) {
    EXPECT_GT(db().fact_count(pool().intern("inventory_item")), 0u);
}

// ── Faction membership: form_model.h registers this as `has_faction`
//    (not `faction` — see kFormArrays[FieldId::Factions]).

TEST_F(NpcGettersTest, HasFactionPopulated) {
    EXPECT_GT(db().fact_count(pool().intern("has_faction")), 0u);
}

} // namespace
