#include "mora/model/validate.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(Validate, VerbShapePairs) {
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Set, Cardinality::Scalar));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Set, Cardinality::Countable));
    EXPECT_FALSE(is_legal_verb_for(VerbKind::Add, Cardinality::Scalar));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Add, Cardinality::Countable));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Add, Cardinality::Set));
    EXPECT_TRUE(is_legal_verb_for(VerbKind::Remove, Cardinality::Set));
    EXPECT_FALSE(is_legal_verb_for(VerbKind::Remove, Cardinality::Scalar));
    EXPECT_FALSE(is_legal_verb_for(VerbKind::Set, Cardinality::Functional));
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
