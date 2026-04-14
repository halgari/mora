#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Relations, RelationEntryHasExpectedFields) {
    constexpr RelationEntry e{
        .namespace_      = "form",
        .name            = "test",
        .args            = {{ElemType::FormRef, "X"}},
        .arg_count       = 1,
        .type            = scalar(ElemType::Int),
        .source          = RelationSourceKind::Static,
        .apply_handler   = HandlerId::None,
        .retract_handler = HandlerId::None,
    };
    EXPECT_EQ(e.namespace_, "form");
    EXPECT_EQ(e.name, "test");
    EXPECT_EQ(e.arg_count, 1);
    EXPECT_EQ(e.type.ctor, TypeCtor::Scalar);
}

TEST(Relations, kRelationsIsAccessible) {
    (void)kRelations;
    (void)kRelationCount;
    SUCCEED();
}
