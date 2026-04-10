#include <gtest/gtest.h>
#include "mora/data/schema_registry.h"
#include "mora/core/string_pool.h"
#include "mora/eval/fact_db.h"

class SchemaRegistryTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(SchemaRegistryTest, RegisterAndLookup) {
    mora::SchemaRegistry reg(pool);
    reg.register_defaults();
    auto* schema = reg.lookup(pool.intern("npc"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 1u);
    EXPECT_EQ(schema->column_types[0].kind, mora::TypeKind::NpcID);
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
    custom.column_types = {mora::MoraType::make(mora::TypeKind::FormID),
                           mora::MoraType::make(mora::TypeKind::String)};
    custom.indexed_columns = {0};
    reg.register_schema(std::move(custom));
    auto* schema = reg.lookup(pool.intern("my_custom_fact"));
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->column_types.size(), 2u);
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
