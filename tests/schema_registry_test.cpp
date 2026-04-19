#include <gtest/gtest.h>
#include "mora/data/schema_registry.h"
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/eval/fact_db.h"

// Register Skyrim nominals before schema tests so mora::types::get("NpcID")
// etc. resolve to non-null pointers. Uses TypeRegistry directly since the
// tests don't have an ExtensionContext.
static void ensure_skyrim_nominals_registered() {
    auto& reg = mora::TypeRegistry::instance();
    auto* i32 = mora::types::int32();
    reg.register_nominal("FormID",     i32);
    reg.register_nominal("NpcID",      i32);
    reg.register_nominal("WeaponID",   i32);
    reg.register_nominal("ArmorID",    i32);
    reg.register_nominal("KeywordID",  i32);
    reg.register_nominal("FactionID",  i32);
    reg.register_nominal("SpellID",    i32);
    reg.register_nominal("PerkID",     i32);
    reg.register_nominal("QuestID",    i32);
    reg.register_nominal("LocationID", i32);
    reg.register_nominal("CellID",     i32);
    reg.register_nominal("RaceID",     i32);
}

class SchemaRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensure_skyrim_nominals_registered();
    }
    mora::StringPool pool;
};

TEST_F(SchemaRegistryTest, RegisterAndLookup) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto* schema = reg.lookup(pool.intern("npc"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 1u);
    EXPECT_EQ(schema->column_types[0], mora::types::get("NpcID"));
    EXPECT_EQ(schema->column_types[0]->name(), "NpcID");
    EXPECT_EQ(schema->column_types[0]->physical(), mora::types::int32());
}

TEST_F(SchemaRegistryTest, HasKeywordSchema) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto* schema = reg.lookup(pool.intern("has_keyword"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
    EXPECT_FALSE(schema->esp_sources.empty());
    // has_keyword should have sources from multiple record types
    EXPECT_GE(schema->esp_sources.size(), 3u);
}

TEST_F(SchemaRegistryTest, WeaponDamageSchema) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto* schema = reg.lookup(pool.intern("damage"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
    ASSERT_FALSE(schema->esp_sources.empty());
    EXPECT_EQ(schema->esp_sources[0].record_type, "WEAP");
    EXPECT_EQ(schema->esp_sources[0].subrecord_tag, "DATA");
}

TEST_F(SchemaRegistryTest, CustomRelation) {
    mora::SchemaRegistry reg(pool);
    mora::RelationSchema custom;
    custom.name = pool.intern("my_custom_fact");
    custom.column_types = {mora::types::get("FormID"), mora::types::get("String")};
    custom.indexed_columns = {0};
    reg.register_schema(std::move(custom));
    auto* schema = reg.lookup(pool.intern("my_custom_fact"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
    EXPECT_EQ(schema->column_types[0], mora::types::get("FormID"));
    EXPECT_EQ(schema->column_types[1], mora::types::get("String"));
}

TEST_F(SchemaRegistryTest, AllRelations) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto all = reg.all_schemas();
    EXPECT_GE(all.size(), 15u);
}

TEST_F(SchemaRegistryTest, SchemasForRecord) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto npc_schemas = reg.schemas_for_record("NPC_");
    // Should include npc, has_keyword, has_faction, has_spell, has_perk, editor_id, name, base_level, race_of
    EXPECT_GE(npc_schemas.size(), 5u);
}

TEST_F(SchemaRegistryTest, ConfigureFactDB) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    mora::FactDB db(pool);
    reg.configure_fact_db(db);
    // Should be able to add facts after configuration
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    EXPECT_EQ(db.fact_count(pool.intern("npc")), 1u);
}

TEST_F(SchemaRegistryTest, ColumnTypesAreNominalPointers) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto* schema = reg.lookup(pool.intern("npc"));
    ASSERT_NE(schema, nullptr);
    ASSERT_EQ(schema->column_types.size(), 1u);
    auto* t = schema->column_types[0];
    ASSERT_NE(t, nullptr);
    EXPECT_TRUE(t->is_nominal());
    EXPECT_EQ(t->physical(), mora::types::int32());
    // Pointer identity: same singleton every time
    EXPECT_EQ(t, mora::types::get("NpcID"));
}
