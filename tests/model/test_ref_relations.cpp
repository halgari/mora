#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RefRelations, RefKeywordIsDynamicList) {
    const auto* r = find_relation("ref", "keyword", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Handler);
    EXPECT_EQ(r->type.ctor, TypeCtor::List);
    EXPECT_EQ(r->type.elem, ElemType::FormRef);
    EXPECT_EQ(r->apply_handler, HandlerId::RefAddKeyword);
    EXPECT_EQ(r->retract_handler, HandlerId::RefRemoveKeyword);
}

TEST(RefRelations, RefCurrentLocationIsConst) {
    const auto* r = find_relation("ref", "current_location", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->source, RelationSourceKind::Handler);
    EXPECT_EQ(r->type.ctor, TypeCtor::Const);
}

TEST(RefRelations, RefBaseFormIsConst) {
    const auto* r = find_relation("ref", "base_form", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::Const);
}

TEST(RefRelations, RefInCombatIsPredicate) {
    const auto* r = find_relation("ref", "in_combat", kRelations, kRelationCount);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->type.ctor, TypeCtor::Predicate);
}
