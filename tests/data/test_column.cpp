#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/column.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"

#include <gtest/gtest.h>

namespace {

TEST(Column, TypeIsCarried) {
    mora::Column c(mora::types::int32());
    EXPECT_EQ(c.type(), mora::types::int32());
    EXPECT_EQ(c.row_count(), 0u);
    EXPECT_EQ(c.chunk_count(), 0u);
}

TEST(Column, AppendsAllocateChunksLazily) {
    mora::Column c(mora::types::int32());
    c.append(mora::Value::make_int(1));
    EXPECT_EQ(c.chunk_count(), 1u);
    EXPECT_EQ(c.row_count(), 1u);
    EXPECT_EQ(c.at(0).as_int(), 1);
}

TEST(Column, RollsToNextChunkAtBoundary) {
    mora::Column c(mora::types::int32());
    for (size_t i = 0; i < mora::kChunkSize + 3; ++i) {
        c.append(mora::Value::make_int(static_cast<int64_t>(i)));
    }
    EXPECT_EQ(c.chunk_count(), 2u);
    EXPECT_EQ(c.row_count(), mora::kChunkSize + 3);
    EXPECT_EQ(c.at(0).as_int(), 0);
    EXPECT_EQ(c.at(mora::kChunkSize - 1).as_int(),
              static_cast<int64_t>(mora::kChunkSize - 1));
    EXPECT_EQ(c.at(mora::kChunkSize).as_int(),
              static_cast<int64_t>(mora::kChunkSize));
    EXPECT_EQ(c.at(mora::kChunkSize + 2).as_int(),
              static_cast<int64_t>(mora::kChunkSize + 2));
}

TEST(Column, FormIDNominalDecodesBack) {
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32());
    mora::Column c(formid);
    c.append(mora::Value::make_formid(0x000A0B0C));
    EXPECT_EQ(c.row_count(), 1u);
    auto const v = c.at(0);
    EXPECT_EQ(v.kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(v.as_formid(), 0x000A0B0Cu);
}

TEST(Column, AnyColumnAcceptsMixedKinds) {
    mora::StringPool pool;
    mora::Column c(mora::types::any());
    c.append(mora::Value::make_int(42));
    c.append(mora::Value::make_string(pool.intern("foo")));
    c.append(mora::Value::make_float(1.5));

    EXPECT_EQ(c.row_count(), 3u);
    EXPECT_EQ(c.at(0).kind(), mora::Value::Kind::Int);
    EXPECT_EQ(c.at(0).as_int(), 42);
    EXPECT_EQ(c.at(1).kind(), mora::Value::Kind::String);
    EXPECT_EQ(pool.get(c.at(1).as_string()), "foo");
    EXPECT_EQ(c.at(2).kind(), mora::Value::Kind::Float);
    EXPECT_EQ(c.at(2).as_float(), 1.5);
}

TEST(Column, KeywordColumnTypedAccess) {
    mora::StringPool pool;
    mora::Column c(mora::types::keyword());
    c.append(mora::Value::make_keyword(pool.intern("GoldValue")));
    c.append(mora::Value::make_keyword(pool.intern("Name")));

    EXPECT_EQ(c.row_count(), 2u);
    EXPECT_EQ(pool.get(c.at(0).as_keyword()), "GoldValue");
    EXPECT_EQ(pool.get(c.at(1).as_keyword()), "Name");

    // Downcast to typed chunk for direct access
    auto const& kw = static_cast<const mora::KeywordVector&>(c.chunk(0));
    EXPECT_EQ(kw.size(), 2u);
}

} // namespace
