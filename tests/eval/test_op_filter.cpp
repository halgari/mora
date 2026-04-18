// Plan 14 M2 — FilterOp unit tests.
// Tests FilterOp in isolation by constructing AST expressions directly and
// driving a ScanOp source, verifying that only rows matching the predicate
// are emitted.

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"
#include "mora/eval/op_filter.h"
#include "mora/eval/op_scan.h"

#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

namespace {

// ── AST construction helpers ────────────────────────────────────────────────

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

// Build an Expr for (lhs_var op rhs_literal) — e.g. ?x >= 5.
mora::Expr cmp_expr(mora::StringId lhs_var, mora::BinaryExpr::Op op, int64_t rhs_val) {
    auto lhs = std::make_unique<mora::Expr>(var_expr(lhs_var));
    auto rhs = std::make_unique<mora::Expr>(int_expr(rhs_val));
    mora::Expr e;
    e.data = mora::BinaryExpr{op, std::move(lhs), std::move(rhs), {}};
    return e;
}

mora::FactPattern pat1(mora::StringId name, mora::Expr a) {
    mora::FactPattern fp;
    fp.name    = name;
    fp.negated = false;
    fp.args.push_back(std::move(a));
    return fp;
}

mora::FactPattern pat2(mora::StringId name, mora::Expr a, mora::Expr b) {
    mora::FactPattern fp;
    fp.name    = name;
    fp.negated = false;
    fp.args.push_back(std::move(a));
    fp.args.push_back(std::move(b));
    return fp;
}

// ── Tests ───────────────────────────────────────────────────────────────────

// FilterOp passes rows where x > 5: out of 10 rows (1..10), 5 should pass.
TEST(FilterOp, IntGtFilter_HalfPass) {
    mora::StringPool pool;
    auto rel_name = pool.intern("src");
    auto x_id     = pool.intern("x");

    mora::ColumnarRelation rel({mora::types::int64()}, {});
    for (int64_t i = 1; i <= 10; ++i)
        rel.append({mora::Value::make_int(i)});

    // ScanOp source
    auto fp   = pat1(rel_name, var_expr(x_id));
    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    // Predicate: x > 5
    auto pred = cmp_expr(x_id, mora::BinaryExpr::Op::Gt, 5);

    mora::FilterOp filter(std::move(scan), &pred, pool, no_syms);

    size_t total = 0;
    int64_t sum  = 0;
    while (auto chunk = filter.next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r) {
            sum   += chunk->cell(r, 0).as_int();
            total += 1;
        }
    }
    // Values > 5: 6+7+8+9+10 = 40, count = 5.
    EXPECT_EQ(total, 5u);
    EXPECT_EQ(sum, 40);
}

// FilterOp passes NO rows when predicate is always false (x > 100).
TEST(FilterOp, AlwaysFalse_NoRows) {
    mora::StringPool pool;
    auto rel_name = pool.intern("r");
    auto x_id     = pool.intern("x");

    mora::ColumnarRelation rel({mora::types::int64()}, {});
    for (int64_t i = 0; i < 5; ++i)
        rel.append({mora::Value::make_int(i)});

    auto fp   = pat1(rel_name, var_expr(x_id));
    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    auto pred = cmp_expr(x_id, mora::BinaryExpr::Op::Gt, 100);
    mora::FilterOp filter(std::move(scan), &pred, pool, no_syms);

    EXPECT_FALSE(filter.next_chunk().has_value());
}

// FilterOp passes ALL rows when predicate is always true (x >= 0).
TEST(FilterOp, AlwaysTrue_AllRows) {
    mora::StringPool pool;
    auto rel_name = pool.intern("r");
    auto x_id     = pool.intern("x");

    mora::ColumnarRelation rel({mora::types::int64()}, {});
    for (int64_t i = 1; i <= 4; ++i)
        rel.append({mora::Value::make_int(i)});

    auto fp   = pat1(rel_name, var_expr(x_id));
    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    auto pred = cmp_expr(x_id, mora::BinaryExpr::Op::GtEq, 0);
    mora::FilterOp filter(std::move(scan), &pred, pool, no_syms);

    size_t total = 0;
    while (auto chunk = filter.next_chunk()) total += chunk->row_count();
    EXPECT_EQ(total, 4u);
}

// FilterOp preserves output_var_names from its input.
TEST(FilterOp, OutputVarNames_PassThrough) {
    mora::StringPool pool;
    auto rel_name = pool.intern("r");
    auto x_id     = pool.intern("x");
    auto y_id     = pool.intern("y");

    mora::ColumnarRelation rel({mora::types::int64(), mora::types::int64()}, {});
    rel.append({mora::Value::make_int(1), mora::Value::make_int(2)});

    auto fp   = pat2(rel_name, var_expr(x_id), var_expr(y_id));
    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    // Remember expected names before moving scan.
    auto expected = scan->output_var_names();

    auto pred = cmp_expr(x_id, mora::BinaryExpr::Op::GtEq, 0);
    mora::FilterOp filter(std::move(scan), &pred, pool, no_syms);

    EXPECT_EQ(filter.output_var_names().size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(filter.output_var_names()[i].index, expected[i].index);
    }
}

// FilterOp on two-column chunk: filter preserves both columns per row.
TEST(FilterOp, TwoColumn_BothColumnsPreserved) {
    mora::StringPool pool;
    auto rel_name = pool.intern("r");
    auto npc_id   = pool.intern("npc");
    auto lvl_id   = pool.intern("lvl");

    mora::ColumnarRelation rel({mora::types::any(), mora::types::int64()}, {});
    // FormID + Level pairs
    rel.append({mora::Value::make_formid(0x10), mora::Value::make_int(5)});   // level 5 — filtered
    rel.append({mora::Value::make_formid(0x20), mora::Value::make_int(15)});  // level 15 — passes
    rel.append({mora::Value::make_formid(0x30), mora::Value::make_int(25)});  // level 25 — passes

    auto fp = pat2(rel_name, var_expr(npc_id), var_expr(lvl_id));
    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    // Predicate: lvl >= 10
    auto pred = cmp_expr(lvl_id, mora::BinaryExpr::Op::GtEq, 10);
    mora::FilterOp filter(std::move(scan), &pred, pool, no_syms);

    std::vector<std::pair<uint32_t, int64_t>> out;
    while (auto chunk = filter.next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r) {
            out.push_back({chunk->cell(r, 0).as_formid(),
                           chunk->cell(r, 1).as_int()});
        }
    }
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].first,  0x20u);
    EXPECT_EQ(out[0].second, 15);
    EXPECT_EQ(out[1].first,  0x30u);
    EXPECT_EQ(out[1].second, 25);
}

} // namespace
