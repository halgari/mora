#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RelationSeed, FormKeywordExists) {
    const auto* r = find_relation("form", "keyword", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::List);
    EXPECT_EQ(r->type.elem, ElemType::FormRef);
    EXPECT_EQ(r->source, RelationSourceKind::Static);
}

TEST(RelationSeed, FormDamageIsCountable) {
    const auto* r = find_relation("form", "damage", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::Countable);
    EXPECT_EQ(r->type.elem, ElemType::Int);
}

TEST(RelationSeed, FormNpcIsPredicate) {
    const auto* r = find_relation("form", "npc", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->arg_count, 1);
    EXPECT_EQ(r->type.ctor, TypeCtor::Predicate);
}
