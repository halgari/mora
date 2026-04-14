#include "mora/model/validate.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Validate, VerbShapePairs) {
    EXPECT_TRUE(is_legal_verb(VerbKind::Set, scalar(ElemType::String)));
    EXPECT_TRUE(is_legal_verb(VerbKind::Set, countable(ElemType::Int)));
    EXPECT_FALSE(is_legal_verb(VerbKind::Add, scalar(ElemType::String)));
    EXPECT_TRUE(is_legal_verb(VerbKind::Add, countable(ElemType::Int)));
    EXPECT_TRUE(is_legal_verb(VerbKind::Add, list_of(ElemType::FormRef)));
    EXPECT_TRUE(is_legal_verb(VerbKind::Remove, list_of(ElemType::FormRef)));
    EXPECT_FALSE(is_legal_verb(VerbKind::Remove, scalar(ElemType::String)));
    EXPECT_FALSE(is_legal_verb(VerbKind::Set, predicate()));
    EXPECT_FALSE(is_legal_verb(VerbKind::Set, const_(ElemType::FormRef)));
}

TEST(Validate, DuplicateDetection) {
    RelationEntry arr[] = {
        {.namespace_ = "a", .name = "x"},
        {.namespace_ = "a", .name = "x"},
    };
    EXPECT_TRUE(has_duplicate(arr, 2));
}

TEST(Validate, DuplicateDetectionNoDuplicates) {
    RelationEntry arr[] = {
        {.namespace_ = "a", .name = "x"},
        {.namespace_ = "a", .name = "y"},
    };
    EXPECT_FALSE(has_duplicate(arr, 2));
}
