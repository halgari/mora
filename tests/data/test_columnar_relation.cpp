#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"

#include <gtest/gtest.h>

namespace {

TEST(ColumnarRelation, AppendsAndRetrievesRows) {
    auto const* formid  = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32(), mora::Value::Kind::FormID);
    mora::ColumnarRelation rel({formid, mora::types::int64()}, /*indexed*/ {0});

    rel.append(mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(42)});
    rel.append(mora::Tuple{mora::Value::make_formid(0x200), mora::Value::make_int(7)});

    EXPECT_EQ(rel.row_count(), 2u);
    EXPECT_EQ(rel.arity(), 2u);

    auto t0 = rel.row_at(0);
    ASSERT_EQ(t0.size(), 2u);
    EXPECT_EQ(t0[0].as_formid(), 0x100u);
    EXPECT_EQ(t0[1].as_int(), 42);

    auto t1 = rel.row_at(1);
    EXPECT_EQ(t1[0].as_formid(), 0x200u);
    EXPECT_EQ(t1[1].as_int(), 7);
}

TEST(ColumnarRelation, QueryByIndexedColumnUsesHashIndex) {
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32(), mora::Value::Kind::FormID);
    mora::ColumnarRelation rel({formid, mora::types::int64()}, /*indexed*/ {0});

    for (uint32_t f = 0; f < 100; ++f) {
        rel.append(mora::Tuple{mora::Value::make_formid(0x1000 + f),
                                mora::Value::make_int(static_cast<int64_t>(f))});
    }

    mora::Tuple pat{mora::Value::make_formid(0x1000 + 42), mora::Value::make_var()};
    auto result = rel.query(pat);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0][0].as_formid(), 0x1000u + 42);
    EXPECT_EQ(result[0][1].as_int(), 42);
}

TEST(ColumnarRelation, QueryFullScanWhenNoIndexMatches) {
    mora::ColumnarRelation rel({mora::types::int64(), mora::types::int64()},
                                /*indexed*/ {});  // no indexes

    rel.append(mora::Tuple{mora::Value::make_int(1), mora::Value::make_int(10)});
    rel.append(mora::Tuple{mora::Value::make_int(2), mora::Value::make_int(20)});
    rel.append(mora::Tuple{mora::Value::make_int(2), mora::Value::make_int(30)});

    mora::Tuple pat{mora::Value::make_int(2), mora::Value::make_var()};
    auto result = rel.query(pat);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0][1].as_int(), 20);
    EXPECT_EQ(result[1][1].as_int(), 30);
}

TEST(ColumnarRelation, ContainsExactMatch) {
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32(), mora::Value::Kind::FormID);
    mora::ColumnarRelation rel({formid, mora::types::int64()}, /*indexed*/ {0});

    rel.append(mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(42)});
    EXPECT_TRUE(rel.contains(
        mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(42)}));
    EXPECT_FALSE(rel.contains(
        mora::Tuple{mora::Value::make_formid(0x100), mora::Value::make_int(99)}));
}

TEST(ColumnarRelation, MaterializeReturnsAllRows) {
    mora::ColumnarRelation rel({mora::types::int64()}, /*indexed*/ {});
    for (int64_t i = 0; i < 5; ++i) {
        rel.append(mora::Tuple{mora::Value::make_int(i)});
    }
    auto rows = rel.materialize();
    ASSERT_EQ(rows.size(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(rows[i][0].as_int(), static_cast<int64_t>(i));
    }
}

TEST(ColumnarRelation, KeywordAndAnyColumnsRoundTrip) {
    mora::StringPool pool;
    auto const* formid = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32(), mora::Value::Kind::FormID);
    mora::ColumnarRelation rel(
        {formid, mora::types::keyword(), mora::types::any()},
        /*indexed*/ {0});

    rel.append(mora::Tuple{
        mora::Value::make_formid(0xAA),
        mora::Value::make_keyword(pool.intern("GoldValue")),
        mora::Value::make_int(100)});
    rel.append(mora::Tuple{
        mora::Value::make_formid(0xBB),
        mora::Value::make_keyword(pool.intern("Name")),
        mora::Value::make_string(pool.intern("Skeever"))});

    ASSERT_EQ(rel.row_count(), 2u);
    auto r0 = rel.row_at(0);
    EXPECT_EQ(r0[0].as_formid(), 0xAAu);
    EXPECT_EQ(pool.get(r0[1].as_keyword()), "GoldValue");
    EXPECT_EQ(r0[2].as_int(), 100);

    auto r1 = rel.row_at(1);
    EXPECT_EQ(r1[0].as_formid(), 0xBBu);
    EXPECT_EQ(pool.get(r1[1].as_keyword()), "Name");
    EXPECT_EQ(pool.get(r1[2].as_string()), "Skeever");
}

TEST(ColumnarRelation, AbsorbBulkMove) {
    mora::ColumnarRelation rel({mora::types::int64()}, /*indexed*/ {0});
    std::vector<mora::Tuple> batch;
    for (int64_t i = 0; i < 10; ++i) batch.push_back({mora::Value::make_int(i)});
    rel.absorb(std::move(batch));
    EXPECT_EQ(rel.row_count(), 10u);

    auto const hit = rel.query({mora::Value::make_int(7)});
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0][0].as_int(), 7);
}

} // namespace
