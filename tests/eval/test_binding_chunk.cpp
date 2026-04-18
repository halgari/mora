#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/value.h"
#include "mora/eval/binding_chunk.h"

#include <gtest/gtest.h>

namespace {

TEST(BindingChunk, ConstructionAndArity) {
    mora::StringPool pool;
    auto x = pool.intern("x");
    auto y = pool.intern("y");

    mora::BindingChunk chunk(
        {x, y},
        {mora::types::int64(), mora::types::keyword()});

    EXPECT_EQ(chunk.arity(), 2u);
    EXPECT_EQ(chunk.row_count(), 0u);
}

TEST(BindingChunk, IndexOf_HitAndMiss) {
    mora::StringPool pool;
    auto x = pool.intern("x");
    auto y = pool.intern("y");
    auto z = pool.intern("z");

    mora::BindingChunk chunk(
        {x, y},
        {mora::types::int64(), mora::types::keyword()});

    EXPECT_EQ(chunk.index_of(x), 0);
    EXPECT_EQ(chunk.index_of(y), 1);
    EXPECT_EQ(chunk.index_of(z), -1);  // absent
}

TEST(BindingChunk, AppendRowAndRowCount) {
    mora::StringPool pool;
    auto a = pool.intern("a");

    mora::BindingChunk chunk({a}, {mora::types::int64()});
    EXPECT_EQ(chunk.row_count(), 0u);

    chunk.append_row({mora::Value::make_int(42)});
    EXPECT_EQ(chunk.row_count(), 1u);

    chunk.append_row({mora::Value::make_int(99)});
    EXPECT_EQ(chunk.row_count(), 2u);
}

TEST(BindingChunk, CellRoundTrip) {
    mora::StringPool pool;
    auto v = pool.intern("v");
    auto kw_hello = pool.intern("Hello");

    mora::BindingChunk chunk({v}, {mora::types::keyword()});
    chunk.append_row({mora::Value::make_keyword(kw_hello)});

    mora::Value cell = chunk.cell(0, 0);
    EXPECT_EQ(cell.kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(cell.as_keyword().index, kw_hello.index);
}

TEST(BindingChunk, MultipleColumnsRoundTrip) {
    mora::StringPool pool;
    auto c0 = pool.intern("form");
    auto c1 = pool.intern("val");

    mora::BindingChunk chunk(
        {c0, c1},
        {mora::types::any(), mora::types::int64()});

    chunk.append_row({mora::Value::make_formid(0xABC), mora::Value::make_int(7)});
    chunk.append_row({mora::Value::make_formid(0xDEF), mora::Value::make_int(8)});

    EXPECT_EQ(chunk.row_count(), 2u);
    EXPECT_EQ(chunk.cell(0, 0).as_formid(), 0xABCu);
    EXPECT_EQ(chunk.cell(0, 1).as_int(),    7);
    EXPECT_EQ(chunk.cell(1, 0).as_formid(), 0xDEFu);
    EXPECT_EQ(chunk.cell(1, 1).as_int(),    8);
}

TEST(BindingChunk, ArityMismatchThrows) {
    mora::StringPool pool;
    auto x = pool.intern("x");

    mora::BindingChunk chunk({x}, {mora::types::int64()});
    EXPECT_THROW(
        chunk.append_row({mora::Value::make_int(1), mora::Value::make_int(2)}),
        std::runtime_error);
}

} // namespace
