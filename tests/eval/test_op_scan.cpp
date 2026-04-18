#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"  // kChunkSize
#include "mora/eval/op_scan.h"

#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

namespace {

mora::Expr var_expr(mora::StringId name) {
    mora::Expr e;
    e.data = mora::VariableExpr{name, {}};
    return e;
}

mora::Expr int_expr(int64_t v) {
    mora::Expr e;
    e.data = mora::IntLiteral{v, {}};
    return e;
}

mora::Expr sym_expr(mora::StringId name) {
    mora::Expr e;
    e.data = mora::SymbolExpr{name, {}};
    return e;
}

// Build a FactPattern with a single arg.
mora::FactPattern pat1(mora::StringId name, mora::Expr a) {
    mora::FactPattern fp;
    fp.name    = name;
    fp.negated = false;
    fp.args.push_back(std::move(a));
    return fp;
}

// Build a FactPattern with two args.
mora::FactPattern pat2(mora::StringId name, mora::Expr a, mora::Expr b) {
    mora::FactPattern fp;
    fp.name    = name;
    fp.negated = false;
    fp.args.push_back(std::move(a));
    fp.args.push_back(std::move(b));
    return fp;
}

// ── Tests ────────────────────────────────────────────────────────────────

// All-variable pattern → all rows returned.
TEST(ScanOp, AllVariables_AllRowsReturned) {
    mora::StringPool pool;
    auto rel_name = pool.intern("src");
    auto x = pool.intern("x");
    auto y = pool.intern("y");

    mora::ColumnarRelation rel({mora::types::any(), mora::types::int64()}, {});
    rel.append({mora::Value::make_formid(0x1), mora::Value::make_int(10)});
    rel.append({mora::Value::make_formid(0x2), mora::Value::make_int(20)});
    rel.append({mora::Value::make_formid(0x3), mora::Value::make_int(30)});

    auto fp = pat2(rel_name, var_expr(x), var_expr(y));

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    size_t total = 0;
    while (auto chunk = scan->next_chunk()) {
        total += chunk->row_count();
    }
    EXPECT_EQ(total, 3u);
}

// Constant-arg filter: only rows with matching constant pass.
TEST(ScanOp, ConstantArgFilter) {
    mora::StringPool pool;
    auto rel_name = pool.intern("rel");
    auto x = pool.intern("x");

    mora::ColumnarRelation rel({mora::types::int64(), mora::types::any()}, {});
    rel.append({mora::Value::make_int(1), mora::Value::make_formid(0xA)});
    rel.append({mora::Value::make_int(2), mora::Value::make_formid(0xB)});
    rel.append({mora::Value::make_int(1), mora::Value::make_formid(0xC)});

    // Pattern: rel(1, ?x) — filter col0 == 1
    auto fp = pat2(rel_name, int_expr(1), var_expr(x));

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    size_t total = 0;
    std::vector<uint32_t> collected;
    while (auto chunk = scan->next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r) {
            collected.push_back(chunk->cell(r, 0).as_formid());
        }
        total += chunk->row_count();
    }
    EXPECT_EQ(total, 2u);
    EXPECT_EQ(collected[0], 0xAu);
    EXPECT_EQ(collected[1], 0xCu);
}

// Duplicate variable → equality filter between two positions.
TEST(ScanOp, DuplicateVarEqualityFilter) {
    mora::StringPool pool;
    auto rel_name = pool.intern("r");
    auto x = pool.intern("x");

    mora::ColumnarRelation rel({mora::types::int64(), mora::types::int64()}, {});
    rel.append({mora::Value::make_int(5), mora::Value::make_int(5)});   // match
    rel.append({mora::Value::make_int(5), mora::Value::make_int(9)});   // no match
    rel.append({mora::Value::make_int(7), mora::Value::make_int(7)});   // match

    // Pattern: r(?x, ?x)
    auto fp = pat2(rel_name, var_expr(x), var_expr(x));

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    size_t total = 0;
    std::vector<int64_t> vals;
    while (auto chunk = scan->next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r) {
            vals.push_back(chunk->cell(r, 0).as_int());
        }
        total += chunk->row_count();
    }
    EXPECT_EQ(total, 2u);
    EXPECT_EQ(vals[0], 5);
    EXPECT_EQ(vals[1], 7);
    // Output arity: deduplicated to 1 column.
    EXPECT_EQ(scan->output_var_names().size(), 1u);
}

// Unresolved SymbolExpr → empty stream (no crash).
TEST(ScanOp, UnresolvedSymbol_EmptyStream) {
    mora::StringPool pool;
    auto rel_name = pool.intern("rel");
    auto sym_name = pool.intern("SomeEditorId");

    mora::ColumnarRelation rel({mora::types::any()}, {});
    rel.append({mora::Value::make_formid(0x1)});

    auto fp = pat1(rel_name, sym_expr(sym_name));

    std::unordered_map<uint32_t, uint32_t> no_syms;  // not resolved
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    EXPECT_FALSE(scan->next_chunk().has_value());
}

// Null relation → empty stream (no crash).
TEST(ScanOp, NullRelation_EmptyStream) {
    mora::StringPool pool;
    auto rel_name = pool.intern("missing");
    auto x = pool.intern("x");

    auto fp = pat1(rel_name, var_expr(x));

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(nullptr, fp, pool, no_syms);

    EXPECT_FALSE(scan->next_chunk().has_value());
}

// Chunking: > kChunkSize rows → multiple chunks, all rows accounted for.
TEST(ScanOp, ChunkingLargeRelation) {
    mora::StringPool pool;
    auto rel_name = pool.intern("big");
    auto x = pool.intern("x");

    mora::ColumnarRelation rel({mora::types::int64()}, {});
    size_t const n = mora::kChunkSize + 100;
    for (size_t i = 0; i < n; ++i) {
        rel.append({mora::Value::make_int(static_cast<int64_t>(i))});
    }

    auto fp = pat1(rel_name, var_expr(x));

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    size_t chunks = 0;
    size_t total  = 0;
    while (auto chunk = scan->next_chunk()) {
        ++chunks;
        total += chunk->row_count();
    }
    EXPECT_EQ(total, n);
    EXPECT_GE(chunks, 2u);
}

} // namespace
