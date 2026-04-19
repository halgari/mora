#include "mora/core/type.h"
#include "mora/data/value.h"

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

TEST(CoreType, PhysicalsHaveNaturalKindHints) {
    EXPECT_EQ(mora::types::int32()->kind_hint(),   mora::Value::Kind::Int);
    EXPECT_EQ(mora::types::int64()->kind_hint(),   mora::Value::Kind::Int);
    EXPECT_EQ(mora::types::float64()->kind_hint(), mora::Value::Kind::Float);
    EXPECT_EQ(mora::types::boolean()->kind_hint(), mora::Value::Kind::Bool);
    EXPECT_EQ(mora::types::string()->kind_hint(),  mora::Value::Kind::String);
    EXPECT_EQ(mora::types::keyword()->kind_hint(), mora::Value::Kind::Keyword);
    EXPECT_EQ(mora::types::any()->kind_hint(),     mora::Value::Kind::Var);
    EXPECT_EQ(mora::types::bytes()->kind_hint(),   mora::Value::Kind::Var);
}

TEST(CoreType, NominalCanOverrideKindHint) {
    auto const* tag = mora::TypeRegistry::instance().register_nominal(
        "Plan12NominalTag", mora::types::int32(), mora::Value::Kind::FormID);
    ASSERT_NE(tag, nullptr);
    EXPECT_TRUE(tag->is_nominal());
    EXPECT_EQ(tag->physical(), mora::types::int32());
    EXPECT_EQ(tag->kind_hint(), mora::Value::Kind::FormID);
}

} // namespace
