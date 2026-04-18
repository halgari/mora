#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"

#include <gtest/gtest.h>

namespace {

TEST(Vector, TypedVectorsCarryTheirType) {
    mora::Int32Vector   i32;
    mora::Int64Vector   i64;
    mora::StringVector  s;
    mora::KeywordVector kw;
    mora::AnyVector     any;

    EXPECT_EQ(i32.type(),  mora::types::int32());
    EXPECT_EQ(i64.type(),  mora::types::int64());
    EXPECT_EQ(s.type(),    mora::types::string());
    EXPECT_EQ(kw.type(),   mora::types::keyword());
    EXPECT_EQ(any.type(),  mora::types::any());
}

TEST(Vector, Int32AppendAndRead) {
    mora::Int32Vector v;
    EXPECT_EQ(v.size(), 0u);
    v.append(42);
    v.append(-7);
    v.append(2'000'000'000);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v.get(0),  42);
    EXPECT_EQ(v.get(1),  -7);
    EXPECT_EQ(v.get(2),  2'000'000'000);
}

TEST(Vector, BoolVectorRoundTrip) {
    mora::BoolVector v;
    v.append(true);
    v.append(false);
    v.append(true);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_TRUE (v.get(0));
    EXPECT_FALSE(v.get(1));
    EXPECT_TRUE (v.get(2));
}

TEST(Vector, StringAndKeywordCarryStringIds) {
    mora::StringPool pool;
    auto const s_id  = pool.intern("Skeever");
    auto const kw_id = pool.intern("Name");

    mora::StringVector  s;
    mora::KeywordVector kw;
    s.append(s_id);
    kw.append(kw_id);

    EXPECT_EQ(s.get(0).index,  s_id.index);
    EXPECT_EQ(kw.get(0).index, kw_id.index);
}

TEST(Vector, BytesVectorVariableWidth) {
    mora::BytesVector b;
    uint8_t const a[] = {1, 2, 3};
    uint8_t const c[] = {9, 8, 7, 6, 5};
    b.append(a, sizeof(a));
    b.append(c, sizeof(c));
    ASSERT_EQ(b.size(), 2u);
    size_t len = 0;
    auto const* p0 = b.data(0, &len);
    EXPECT_EQ(len, 3u);
    EXPECT_EQ(p0[0], 1); EXPECT_EQ(p0[2], 3);
    auto const* p1 = b.data(1, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(p1[0], 9); EXPECT_EQ(p1[4], 5);
}

TEST(Vector, AnyVectorMixedKinds) {
    mora::StringPool pool;
    mora::AnyVector v;
    v.append(mora::Value::make_int(100));
    v.append(mora::Value::make_float(2.5));
    v.append(mora::Value::make_string(pool.intern("hello")));
    v.append(mora::Value::make_formid(0xDEADBEEF));

    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v.kind_at(0), mora::Value::Kind::Int);
    EXPECT_EQ(v.kind_at(1), mora::Value::Kind::Float);
    EXPECT_EQ(v.kind_at(2), mora::Value::Kind::String);
    EXPECT_EQ(v.kind_at(3), mora::Value::Kind::FormID);

    EXPECT_EQ(v.get(0).as_int(),     100);
    EXPECT_EQ(v.get(1).as_float(),   2.5);
    EXPECT_EQ(pool.get(v.get(2).as_string()), "hello");
    EXPECT_EQ(v.get(3).as_formid(),  0xDEADBEEFu);
}

} // namespace
