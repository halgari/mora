#include <gtest/gtest.h>
#include "mora/esp/esp_reader.h"
#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"
#include <filesystem>

static const char* SKYRIM_ESM = "/home/tbaldrid/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";

class EspReaderTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    bool skyrim_available() { return std::filesystem::exists(SKYRIM_ESM); }
};

TEST_F(EspReaderTest, ReadSkyrimNPCs) {
    if (!skyrim_available()) GTEST_SKIP();

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(SKYRIM_ESM, db);

    // Skyrim.esm has thousands of NPCs
    auto npc_count = db.fact_count(pool.intern("npc"));
    EXPECT_GT(npc_count, 1000u);

    printf("  NPCs: %zu\n", npc_count);
    printf("  Total records: %zu\n", reader.records_processed());
    printf("  Total facts: %zu\n", reader.facts_generated());
}

TEST_F(EspReaderTest, ReadSkyrimWeapons) {
    if (!skyrim_available()) GTEST_SKIP();

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(SKYRIM_ESM, db);

    auto weapon_count = db.fact_count(pool.intern("weapon"));
    EXPECT_GT(weapon_count, 100u);

    // Check that damage facts were extracted
    auto damage_count = db.fact_count(pool.intern("damage"));
    EXPECT_GT(damage_count, 0u);

    printf("  Weapons: %zu\n", weapon_count);
    printf("  Damage facts: %zu\n", damage_count);
}

TEST_F(EspReaderTest, ReadSkyrimKeywords) {
    if (!skyrim_available()) GTEST_SKIP();

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(SKYRIM_ESM, db);

    auto keyword_count = db.fact_count(pool.intern("keyword"));
    EXPECT_GT(keyword_count, 100u);

    // has_keyword should have many entries (NPCs + weapons + armor all have keywords)
    auto has_kw_count = db.fact_count(pool.intern("has_keyword"));
    EXPECT_GT(has_kw_count, 1000u);

    printf("  Keywords: %zu\n", keyword_count);
    printf("  has_keyword facts: %zu\n", has_kw_count);
}

TEST_F(EspReaderTest, EditorIDResolution) {
    if (!skyrim_available()) GTEST_SKIP();

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(SKYRIM_ESM, db);

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
    if (!skyrim_available()) GTEST_SKIP();

    mora::SchemaRegistry schema(pool);
    schema.register_defaults();

    mora::FactDB db(pool);
    schema.configure_fact_db(db);

    mora::EspReader reader(pool, diags, schema);
    reader.read_plugin(SKYRIM_ESM, db);

    auto faction_count = db.fact_count(pool.intern("faction"));
    EXPECT_GT(faction_count, 50u);

    auto has_faction_count = db.fact_count(pool.intern("has_faction"));
    EXPECT_GT(has_faction_count, 100u);

    printf("  Factions: %zu\n", faction_count);
    printf("  has_faction facts: %zu\n", has_faction_count);
}
