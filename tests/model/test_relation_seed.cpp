#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RelationSeed, FormKeywordExists) {
    const auto* r = find_relation("form", "keyword", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Set);
    EXPECT_EQ(r->source, RelationSourceKind::Static);
}

TEST(RelationSeed, FormDamageIsCountable) {
    const auto* r = find_relation("form", "damage", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->cardinality, Cardinality::Countable);
    EXPECT_EQ(r->value_type, RelValueType::Int);
}

TEST(RelationSeed, FormNpcIsFunctionalPredicate) {
    const auto* r = find_relation("form", "npc", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->arg_count, 1);
}
