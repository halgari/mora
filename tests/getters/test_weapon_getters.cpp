//
// Unit tests for every static getter declared against WEAP records.
//
// Asserts extracted values against known-good FormIDs in vanilla Skyrim.esm:
//   • Iron Sword 0x00012EB7 — 1H melee, canonical baseline.
//
// See data/relations/form/weapon.yaml and include/mora/data/form_model.h
// for the relation catalogue this file covers.

#include "skyrim_fixture.h"
#include <gtest/gtest.h>

namespace {

constexpr uint32_t kIronSword = 0x00012EB7;

class WeaponGettersTest : public mora::test::SkyrimTest {};

// ── Existence predicate (form_model.h kFormTypes) ────────────────────

TEST_F(WeaponGettersTest, WeaponExistencePredicate) {
    EXPECT_TRUE(has_fact_for("weapon", kIronSword));
}

// ── damage: form_model.h kEspSources WEAP/DATA@8 as Int16 ────────────

TEST_F(WeaponGettersTest, IronSwordDamage) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("damage", kIronSword, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Int);
    EXPECT_EQ(v.as_int(), 7);
}

// ── speed: YAML-sourced, WEAP/DNAM packed_field @4 float32 ───────────

TEST_F(WeaponGettersTest, IronSwordSpeed) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("speed", kIronSword, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Float);
    EXPECT_FLOAT_EQ(static_cast<float>(v.as_float()), 1.0f);
}

// ── reach: YAML-sourced, WEAP/DNAM packed_field @8 float32 ───────────

TEST_F(WeaponGettersTest, IronSwordReach) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("reach", kIronSword, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Float);
    EXPECT_FLOAT_EQ(static_cast<float>(v.as_float()), 1.0f);
}

} // namespace
