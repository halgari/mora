#include "mora/model/relation_types.h"
#include <gtest/gtest.h>

using namespace mora::model;

TEST(RelationTypes, ValueTypeEnumHasExpectedValues) {
    EXPECT_EQ(static_cast<int>(RelValueType::Int), 0);
    EXPECT_EQ(static_cast<int>(RelValueType::Float), 1);
    EXPECT_EQ(static_cast<int>(RelValueType::String), 2);
    EXPECT_EQ(static_cast<int>(RelValueType::FormRef), 3);
    EXPECT_EQ(static_cast<int>(RelValueType::Keyword), 4);
}

TEST(RelationTypes, CardinalityEnum) {
    EXPECT_NE(Cardinality::Scalar, Cardinality::Countable);
    EXPECT_NE(Cardinality::Countable, Cardinality::Set);
    EXPECT_NE(Cardinality::Set, Cardinality::Functional);
}

TEST(RelationTypes, ArgSpecDefaultsToInt) {
    constexpr ArgSpec a;
    EXPECT_EQ(a.type, RelValueType::Int);
}
