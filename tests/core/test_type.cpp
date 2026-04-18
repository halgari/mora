#include "mora/core/type.h"

#include <gtest/gtest.h>

namespace {

TEST(CoreType, PhysicalSingletonsAreStable) {
    EXPECT_EQ(mora::types::int32(),   mora::types::int32());
    EXPECT_EQ(mora::types::string(),  mora::types::string());
    EXPECT_NE(mora::types::string(),  mora::types::keyword());
    EXPECT_NE(mora::types::int32(),   mora::types::int64());
}

TEST(CoreType, PhysicalNamesAndShape) {
    EXPECT_EQ(mora::types::int32()->name(),   "Int32");
    EXPECT_EQ(mora::types::keyword()->name(), "Keyword");
    EXPECT_FALSE(mora::types::int32()->is_nominal());
    EXPECT_EQ(mora::types::int32()->physical(), mora::types::int32());
}

TEST(CoreType, TypeRegistryFindsPhysicals) {
    EXPECT_EQ(mora::types::get("Int32"),   mora::types::int32());
    EXPECT_EQ(mora::types::get("String"),  mora::types::string());
    EXPECT_EQ(mora::types::get("Keyword"), mora::types::keyword());
    EXPECT_EQ(mora::types::get("NotAType"), nullptr);
}

TEST(CoreType, RegisterNominalIsIdempotent) {
    auto& reg = mora::TypeRegistry::instance();
    auto const* a = reg.register_nominal("PlanNineTestTag", mora::types::int32());
    auto const* b = reg.register_nominal("PlanNineTestTag", mora::types::int32());
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(a->is_nominal());
    EXPECT_EQ(a->physical(), mora::types::int32());
    EXPECT_EQ(a->name(), "PlanNineTestTag");
    EXPECT_EQ(mora::types::get("PlanNineTestTag"), a);
}

} // namespace
