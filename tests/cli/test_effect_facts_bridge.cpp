#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/effect_facts.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/model/relations.h"

#include <gtest/gtest.h>

namespace {

TEST(EffectFactsBridge, OneEntryPerOp) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchSet ps;

    auto const form_id = uint32_t{0x000DEFEA};
    auto const field   = mora::FieldId::GoldValue;

    ps.add_patch(form_id, field, mora::FieldOp::Set,
                 mora::Value::make_int(100),
                 mora::StringId{}, /*priority*/ 0);
    ps.add_patch(form_id, field, mora::FieldOp::Add,
                 mora::Value::make_int(50),
                 mora::StringId{}, /*priority*/ 0);
    ps.add_patch(form_id, field, mora::FieldOp::Remove,
                 mora::Value::make_int(25),
                 mora::StringId{}, /*priority*/ 0);
    ps.add_patch(form_id, field, mora::FieldOp::Multiply,
                 mora::Value::make_int(2),
                 mora::StringId{}, /*priority*/ 0);

    mora::populate_effect_facts(ps.resolve(), db, pool);

    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        ASSERT_EQ(db.fact_count(rel_id), 1U)
            << "relation " << rel_name << " has unexpected count";
    }

    auto id_set = pool.intern("skyrim/set");
    const auto& set_tuples = db.get_relation(id_set);
    ASSERT_EQ(set_tuples.size(), 1U);
    const auto& t = set_tuples.front();
    ASSERT_EQ(t.size(), 3U);
    EXPECT_EQ(t[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(t[0].as_formid(), form_id);
    EXPECT_EQ(t[1].kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
    EXPECT_EQ(t[2].kind(), mora::Value::Kind::Int);
    EXPECT_EQ(t[2].as_int(), 100);
}

TEST(EffectFactsBridge, EmptyPatchSetPopulatesNothing) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchSet ps;

    mora::populate_effect_facts(ps.resolve(), db, pool);

    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        EXPECT_EQ(db.fact_count(rel_id), 0U)
            << "relation " << rel_name << " should be empty";
    }
}

TEST(EffectFactsBridge, StringFloatAndFormIDValuesRoundTrip) {
    // Covers the three non-Int decode paths — all now work without
    // any special-case handling, because fp.value carries the typed
    // Value directly (Plan 7's key benefit over the Plan 5 byte-packed
    // PatchBuffer approach).
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchSet ps;

    auto form = uint32_t{0x000A0B0C};

    auto const interned_name = pool.intern("Skeever");
    ps.add_patch(form, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(interned_name),
                 mora::StringId{}, /*priority*/ 0);

    double const damage = 12.5;
    ps.add_patch(form, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_float(damage),
                 mora::StringId{}, /*priority*/ 0);

    uint32_t const race_form = 0x00013746;
    ps.add_patch(form, mora::FieldId::Race, mora::FieldOp::Set,
                 mora::Value::make_formid(race_form),
                 mora::StringId{}, /*priority*/ 0);

    mora::populate_effect_facts(ps.resolve(), db, pool);

    auto id_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(id_set);
    ASSERT_EQ(tuples.size(), 3U);

    const mora::Tuple* name_tuple   = nullptr;
    const mora::Tuple* damage_tuple = nullptr;
    const mora::Tuple* race_tuple   = nullptr;
    for (const auto& t : tuples) {
        auto kw = pool.get(t[1].as_keyword());
        if      (kw == "Name")   name_tuple   = &t;
        else if (kw == "Damage") damage_tuple = &t;
        else if (kw == "Race")   race_tuple   = &t;
    }
    ASSERT_NE(name_tuple,   nullptr);
    ASSERT_NE(damage_tuple, nullptr);
    ASSERT_NE(race_tuple,   nullptr);

    EXPECT_EQ((*name_tuple)[2].kind(), mora::Value::Kind::String);
    EXPECT_EQ(pool.get((*name_tuple)[2].as_string()), "Skeever");

    EXPECT_EQ((*damage_tuple)[2].kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ((*damage_tuple)[2].as_float(), damage);

    EXPECT_EQ((*race_tuple)[2].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ((*race_tuple)[2].as_formid(), race_form);
}

} // namespace
