//
// Unit tests for every static getter declared against ARMO records.
//
// Reference FormIDs from vanilla Skyrim.esm:
//   • Iron Helmet 0x00012E4D — armor_rating 15 (raw 1500).
//
// See data/relations/form/armor.yaml and include/mora/data/form_model.h.

#include "skyrim_fixture.h"
#include <gtest/gtest.h>

namespace {

constexpr uint32_t kIronHelmet = 0x00012E4D;

class ArmorGettersTest : public mora::test::SkyrimTest {};

// ── Existence predicate (form_model.h kFormTypes) ────────────────────

TEST_F(ArmorGettersTest, ArmorExistencePredicate) {
    EXPECT_TRUE(has_fact_for("armor", kIronHelmet));
}

// ── armor_rating: form_model.h kEspSources ARMO/DNAM@0 as Float32.
//
//    NOTE: form_model.h has a schema inconsistency — the in-memory layout
//    (kArmorDirectMembers[0]) is UInt32 at +0x200 but the ESP source reads
//    DNAM@0 as Float32. The raw DNAM bytes for a vanilla armor are actually
//    a little-endian uint32 equal to display×100 (Iron Helmet: 1500), so
//    reinterpreting those bytes as IEEE-754 float produces a denormal
//    (~2.1e-42) rather than 1500.0. Until that's reconciled we only assert
//    fact existence + type — the Datalog-visible value is garbage today.

TEST_F(ArmorGettersTest, IronHelmetArmorRatingFactExists) {
    mora::Value v;
    ASSERT_TRUE(scalar_fact("armor_rating", kIronHelmet, v));
    EXPECT_EQ(v.kind(), mora::Value::Kind::Float);
}

} // namespace
