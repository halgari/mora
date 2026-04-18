#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/emit/patch_table.h"
#include "mora/eval/effect_facts.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"

#include <bit>
#include <gtest/gtest.h>

namespace {

TEST(EffectFactsBridge, OneEntryPerOp) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    auto const form_id = uint32_t{0x000DEFEA};
    auto const field   = static_cast<uint8_t>(mora::FieldId::GoldValue);

    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 100);
    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Add),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 50);
    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Remove),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 25);
    buf.emit(form_id, field, static_cast<uint8_t>(mora::FieldOp::Multiply),
             static_cast<uint8_t>(mora::PatchValueType::Int), /*value*/ 2);

    mora::populate_effect_facts(buf, db, pool);

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

TEST(EffectFactsBridge, EmptyBufferPopulatesNothing) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    mora::populate_effect_facts(buf, db, pool);

    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        EXPECT_EQ(db.fact_count(rel_id), 0U)
            << "relation " << rel_name << " should be empty";
    }
}

TEST(EffectFactsBridge, FloatAndFormIDValuesRoundTrip) {
    // Covers the two non-Int decode paths that ARE supported:
    //   PatchValueType::Float      → Value::Kind::Float
    //   PatchValueType::FormID     → Value::Kind::FormID
    // (PatchValueType::StringIndex is intentionally skipped by the
    // bridge today — see the dedicated test below for that behavior.)
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    auto form = uint32_t{0x000A0B0C};
    auto field_dmg_id   = static_cast<uint8_t>(mora::FieldId::Damage);
    auto field_race_id  = static_cast<uint8_t>(mora::FieldId::Race);

    double const damage = 12.5;
    buf.emit(form, field_dmg_id,
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Float),
             std::bit_cast<uint64_t>(damage));

    uint32_t const race_form = 0x00013746;  // an arbitrary Race FormID
    buf.emit(form, field_race_id,
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::FormID),
             static_cast<uint64_t>(race_form));

    mora::populate_effect_facts(buf, db, pool);

    auto id_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(id_set);
    ASSERT_EQ(tuples.size(), 2U);

    const mora::Tuple* damage_tuple = nullptr;
    const mora::Tuple* race_tuple   = nullptr;
    for (const auto& t : tuples) {
        auto kw = pool.get(t[1].as_keyword());
        if (kw == "Damage") damage_tuple = &t;
        else if (kw == "Race") race_tuple = &t;
    }
    ASSERT_NE(damage_tuple, nullptr);
    ASSERT_NE(race_tuple, nullptr);

    EXPECT_EQ((*damage_tuple)[2].kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ((*damage_tuple)[2].as_float(), damage);

    EXPECT_EQ((*race_tuple)[2].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ((*race_tuple)[2].as_formid(), race_form);
}

TEST(EffectFactsBridge, StringIndexEntriesAreSkipped) {
    // In the production PatchBuffer, PatchValueType::StringIndex entries
    // encode a byte offset into the mora_patches.bin StringTable blob,
    // NOT a StringPool StringId. Decoding them naively would emit
    // garbage strings. The bridge documents this and skips such entries
    // today; a later plan will route string-valued effects through a
    // design that has access to the string table (or the upstream
    // typed PatchSet).
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchBuffer buf;

    auto form = uint32_t{0x00010203};
    auto field_name_id = static_cast<uint8_t>(mora::FieldId::Name);

    // Emit a StringIndex entry — bogus byte-offset value; the bridge
    // must skip it regardless of the numeric value.
    buf.emit(form, field_name_id,
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::StringIndex),
             /*value (byte offset, not pool index)*/ 42);

    // Also emit a regular Int entry to confirm the skip is
    // entry-scoped, not bridge-wide.
    auto field_gold_id = static_cast<uint8_t>(mora::FieldId::GoldValue);
    buf.emit(form, field_gold_id,
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Int),
             /*value*/ 500);

    mora::populate_effect_facts(buf, db, pool);

    auto id_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(id_set);
    ASSERT_EQ(tuples.size(), 1U);  // only the Int entry, StringIndex skipped
    EXPECT_EQ(pool.get(tuples.front()[1].as_keyword()), "GoldValue");
    EXPECT_EQ(tuples.front()[2].kind(), mora::Value::Kind::Int);
    EXPECT_EQ(tuples.front()[2].as_int(), 500);
}

} // namespace
