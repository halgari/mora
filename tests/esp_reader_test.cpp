#include <gtest/gtest.h>
#include "mora/esp/esp_reader.h"
#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"
#include "skyrim_fixture.h"
#include <filesystem>
#include <string>

// Skyrim.esm is a hard prerequisite — resolved via skyrim_fixture.h
// (env var / CI path / dev-box path). Tests intentionally do not skip
// when missing; the shared fixture aborts the binary with a clear
// message if the data dir can't be found.
static std::string skyrim_esm() {
    return mora::test::skyrim_esm_path().string();
}

class EspReaderTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(EspReaderTest, ReadSkyrimNPCs) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    // Skyrim.esm has thousands of NPCs
    auto npc_count = db.fact_count(pool.intern("npc"));
    EXPECT_GT(npc_count, 1000u);

    printf("  NPCs: %zu\n", npc_count);
    printf("  Total records: %zu\n", reader.records_processed());
    printf("  Total facts: %zu\n", reader.facts_generated());
}

TEST_F(EspReaderTest, ReadSkyrimWeapons) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto weapon_count = db.fact_count(pool.intern("weapon"));
    EXPECT_GT(weapon_count, 100u);

    // Check that damage facts were extracted
    auto damage_count = db.fact_count(pool.intern("damage"));
    EXPECT_GT(damage_count, 0u);

    printf("  Weapons: %zu\n", weapon_count);
    printf("  Damage facts: %zu\n", damage_count);
}

TEST_F(EspReaderTest, ReadSkyrimKeywords) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto keyword_count = db.fact_count(pool.intern("keyword"));
    EXPECT_GT(keyword_count, 100u);

    // has_keyword should have many entries (NPCs + weapons + armor all have keywords)
    auto has_kw_count = db.fact_count(pool.intern("has_keyword"));
    EXPECT_GT(has_kw_count, 1000u);

    printf("  Keywords: %zu\n", keyword_count);
    printf("  has_keyword facts: %zu\n", has_kw_count);
}

TEST_F(EspReaderTest, EditorIDResolution) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    // "IronSword" should be a known EditorID
    auto formid = reader.resolve_symbol("IronSword");
    EXPECT_NE(formid, 0u);

    // "ActorTypeNPC" should be a keyword EditorID
    auto kw_formid = reader.resolve_symbol("ActorTypeNPC");
    EXPECT_NE(kw_formid, 0u);

    printf("  IronSword FormID: 0x%08X\n", formid);
    printf("  ActorTypeNPC FormID: 0x%08X\n", kw_formid);
    printf("  Total EditorIDs: %zu\n", reader.editor_id_map().size());
}

TEST_F(EspReaderTest, ReadSkyrimFactions) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto faction_count = db.fact_count(pool.intern("faction"));
    EXPECT_GT(faction_count, 50u);

    auto has_faction_count = db.fact_count(pool.intern("has_faction"));
    EXPECT_GT(has_faction_count, 100u);

    printf("  Factions: %zu\n", faction_count);
    printf("  has_faction facts: %zu\n", has_faction_count);
}

// ══════════════════════════════════════════════════════════════════════
// End-to-end tests for YAML-declared extractions (Plan-2-phase follow-up).
// These exercise the YAML → SchemaRegistry bridge against real plugin
// data, validating that the new extraction kinds (BitTest, PackedField at
// declared offsets, ListField on LVLO, Subrecord reads for const<FormRef>)
// produce the expected facts.
// ══════════════════════════════════════════════════════════════════════

TEST_F(EspReaderTest, NPCFlagPredicatesExtract) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();
    mora::FactDB db(pool);
    schema.configure_fact_db(db);
    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    // Every ACBS flag bit predicate should be populated for SOME NPCs.
    auto essential_count = db.fact_count(pool.intern("essential"));
    auto protected_count = db.fact_count(pool.intern("protected"));
    auto unique_count    = db.fact_count(pool.intern("unique"));
    auto female_count    = db.fact_count(pool.intern("female"));

    EXPECT_GT(essential_count, 0u);
    EXPECT_GT(protected_count, 0u);
    EXPECT_GT(unique_count, 0u);
    EXPECT_GT(female_count, 0u);

    printf("  essential: %zu  protected: %zu  unique: %zu  female: %zu\n",
           essential_count, protected_count, unique_count, female_count);
}

TEST_F(EspReaderTest, NPCPackedStatsExtract) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();
    mora::FactDB db(pool);
    schema.configure_fact_db(db);
    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto calc_min = db.fact_count(pool.intern("calc_level_min"));
    auto calc_max = db.fact_count(pool.intern("calc_level_max"));
    auto speed    = db.fact_count(pool.intern("speed_mult"));

    // Every NPC record should produce one fact for each of these.
    auto npc_count = db.fact_count(pool.intern("npc"));
    EXPECT_EQ(calc_min, npc_count);
    EXPECT_EQ(calc_max, npc_count);
    EXPECT_EQ(speed,    npc_count);

    printf("  NPCs: %zu   calc_min/max/speed facts: %zu/%zu/%zu\n",
           npc_count, calc_min, calc_max, speed);
}

TEST_F(EspReaderTest, NPCConstRefsExtract) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();
    mora::FactDB db(pool);
    schema.configure_fact_db(db);
    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto cls = db.fact_count(pool.intern("npc_class"));
    auto vtp = db.fact_count(pool.intern("voice_type"));
    auto out = db.fact_count(pool.intern("default_outfit"));

    EXPECT_GT(cls, 100u);  // almost every NPC has a class
    EXPECT_GT(vtp, 0u);
    EXPECT_GT(out, 0u);

    printf("  npc_class: %zu  voice_type: %zu  default_outfit: %zu\n",
           cls, vtp, out);
}

TEST_F(EspReaderTest, NPCSpellAndPerkListsExtract) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();
    mora::FactDB db(pool);
    schema.configure_fact_db(db);
    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto spell_count = db.fact_count(pool.intern("spell"));
    auto perk_count  = db.fact_count(pool.intern("perk"));
    auto inv_count   = db.fact_count(pool.intern("inventory_item"));

    EXPECT_GT(spell_count, 0u);
    EXPECT_GT(perk_count, 0u);
    EXPECT_GT(inv_count, 0u);

    printf("  spell: %zu  perk: %zu  inventory_item: %zu\n",
           spell_count, perk_count, inv_count);
}

TEST_F(EspReaderTest, WeaponPackedFieldsExtract) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();
    mora::FactDB db(pool);
    schema.configure_fact_db(db);
    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto speed_count = db.fact_count(pool.intern("speed"));
    auto reach_count = db.fact_count(pool.intern("reach"));
    auto weapon_count = db.fact_count(pool.intern("weapon"));

    // Most weapons have a DNAM subrecord carrying both speed and reach.
    EXPECT_GT(speed_count, 0u);
    EXPECT_GT(reach_count, 0u);
    EXPECT_GE(weapon_count, speed_count);

    printf("  weapons: %zu  speed: %zu  reach: %zu\n",
           weapon_count, speed_count, reach_count);
}

TEST_F(EspReaderTest, LeveledListsExtract) {

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();
    mora::FactDB db(pool);
    schema.configure_fact_db(db);
    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(skyrim_esm(), db);

    auto list_count  = db.fact_count(pool.intern("leveled_list"));
    auto entry_count = db.fact_count(pool.intern("leveled_entry"));
    auto chance_none_count = db.fact_count(pool.intern("chance_none"));

    EXPECT_GT(list_count, 100u);
    EXPECT_GT(entry_count, list_count);  // each list typically has several entries
    EXPECT_GT(chance_none_count, 0u);

    printf("  leveled_list: %zu  leveled_entry: %zu  chance_none: %zu\n",
           list_count, entry_count, chance_none_count);
}
