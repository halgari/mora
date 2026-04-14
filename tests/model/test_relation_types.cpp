#include "mora/model/relation_types.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RelationTypes, TypeCtorEnumDistinct) {
    EXPECT_NE(TypeCtor::Scalar, TypeCtor::List);
    EXPECT_NE(TypeCtor::Countable, TypeCtor::Predicate);
    EXPECT_NE(TypeCtor::Const, TypeCtor::Scalar);
}

TEST(RelationTypes, TypeExprBuilders) {
    auto t = list_of(ElemType::FormRef);
    EXPECT_EQ(t.ctor, TypeCtor::List);
    EXPECT_EQ(t.elem, ElemType::FormRef);

    auto s = scalar(ElemType::String);
    EXPECT_EQ(s.ctor, TypeCtor::Scalar);
    EXPECT_EQ(s.elem, ElemType::String);

    auto c = countable(ElemType::Int);
    EXPECT_EQ(c.ctor, TypeCtor::Countable);

    auto k = const_(ElemType::FormRef);
    EXPECT_EQ(k.ctor, TypeCtor::Const);

    auto p = predicate();
    EXPECT_EQ(p.ctor, TypeCtor::Predicate);
}

TEST(RelationTypes, CtorSpecListHasAddAndRemove) {
    const auto& s = ctor_spec(TypeCtor::List);
    EXPECT_EQ(s.verb_count, 2u);
    EXPECT_TRUE(is_legal_verb(VerbKind::Add, list_of(ElemType::FormRef)));
    EXPECT_TRUE(is_legal_verb(VerbKind::Remove, list_of(ElemType::FormRef)));
    EXPECT_FALSE(is_legal_verb(VerbKind::Set, list_of(ElemType::FormRef)));
}

TEST(RelationTypes, CtorSpecScalarOnlySet) {
    EXPECT_TRUE(is_legal_verb(VerbKind::Set, scalar(ElemType::String)));
    EXPECT_FALSE(is_legal_verb(VerbKind::Add, scalar(ElemType::String)));
    EXPECT_FALSE(is_legal_verb(VerbKind::Remove, scalar(ElemType::String)));
}

TEST(RelationTypes, CtorSpecCountable) {
    auto t = countable(ElemType::Int);
    EXPECT_TRUE(is_legal_verb(VerbKind::Set, t));
    EXPECT_TRUE(is_legal_verb(VerbKind::Add, t));
    EXPECT_TRUE(is_legal_verb(VerbKind::Sub, t));
    EXPECT_FALSE(is_legal_verb(VerbKind::Remove, t));
}

TEST(RelationTypes, CtorSpecPredicateIsReadOnly) {
    EXPECT_FALSE(ctor_spec(TypeCtor::Predicate).writable);
    EXPECT_EQ(ctor_spec(TypeCtor::Predicate).verb_count, 0u);
}

TEST(RelationTypes, CtorSpecConstIsReadOnly) {
    EXPECT_FALSE(ctor_spec(TypeCtor::Const).writable);
    EXPECT_EQ(ctor_spec(TypeCtor::Const).verb_count, 0u);
    EXPECT_FALSE(is_legal_verb(VerbKind::Set, const_(ElemType::FormRef)));
}

TEST(RelationTypes, ArgSpecDefaultsToInt) {
    constexpr ArgSpec a;
    EXPECT_EQ(a.type, ElemType::Int);
}
