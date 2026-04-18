// Plan 14 M3 — InClauseOp unit tests.
// Tests both generator and membership forms in isolation by building ScanOp
// sources over seeded ColumnarRelations and driving InClauseOp directly.

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"
#include "mora/eval/op_in_clause.h"
#include "mora/eval/op_scan.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

namespace {

// ── Helpers ─────────────────────────────────────────────────────────────────

mora::Expr var_expr(mora::StringId name) {
    mora::Expr e;
    e.data = mora::VariableExpr{name, {}};
    return e;
}

// ── Generator tests ──────────────────────────────────────────────────────────

// Generator form: one input row with a 3-element list → 3 output rows.
TEST(InClauseOp, Generator_OneRowThreeElements) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    // Seed a relation with one row: (List[0xA00, 0xB00, 0xC00])
    mora::ColumnarRelation rel({mora::types::any()}, {});
    mora::Value kw_list = mora::Value::make_list({
        mora::Value::make_formid(0xA00),
        mora::Value::make_formid(0xB00),
        mora::Value::make_formid(0xC00),
    });
    rel.append({kw_list});

    auto list_var = pool.intern("KwList");
    mora::FactPattern fp;
    fp.name    = pool.intern("kw_data");
    fp.negated = false;
    fp.args.push_back(var_expr(list_var));

    auto scan = mora::ScanOp::build(&rel, fp, pool, no_syms);

    // Generator: ?Kw in ?KwList — KwList is bound by scan, Kw is new.
    auto kw_var      = pool.intern("Kw");
    auto values_expr = var_expr(list_var);  // RHS is the bound list variable

    auto gen = mora::InClauseOp::build_generator(
        std::move(scan), kw_var, &values_expr, pool, no_syms);

    // Output var names should be: KwList, Kw
    auto const& names = gen->output_var_names();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0].index, list_var.index);
    EXPECT_EQ(names[1].index, kw_var.index);

    // Collect output
    std::vector<uint32_t> out;
    while (auto chunk = gen->next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r) {
            // Column 1 is the generated Kw
            out.push_back(chunk->cell(r, 1).as_formid());
        }
    }

    ASSERT_EQ(out.size(), 3u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0], 0xA00u);
    EXPECT_EQ(out[1], 0xB00u);
    EXPECT_EQ(out[2], 0xC00u);
}

// Generator form: two input rows each with a 2-element list → 4 output rows.
TEST(InClauseOp, Generator_TwoRowsTwoElementsEach) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    mora::ColumnarRelation rel({mora::types::any()}, {});
    rel.append({mora::Value::make_list({mora::Value::make_formid(0x101),
                                        mora::Value::make_formid(0x102)})});
    rel.append({mora::Value::make_list({mora::Value::make_formid(0x103),
                                        mora::Value::make_formid(0x104)})});

    auto list_var = pool.intern("L");
    mora::FactPattern fp;
    fp.name    = pool.intern("data");
    fp.negated = false;
    fp.args.push_back(var_expr(list_var));

    auto scan        = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto kw_var      = pool.intern("E");
    auto values_expr = var_expr(list_var);

    auto gen = mora::InClauseOp::build_generator(
        std::move(scan), kw_var, &values_expr, pool, no_syms);

    std::vector<uint32_t> out;
    while (auto chunk = gen->next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r)
            out.push_back(chunk->cell(r, 1).as_formid());
    }

    ASSERT_EQ(out.size(), 4u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0], 0x101u);
    EXPECT_EQ(out[1], 0x102u);
    EXPECT_EQ(out[2], 0x103u);
    EXPECT_EQ(out[3], 0x104u);
}

// Generator form: empty list → zero rows emitted for that input row.
TEST(InClauseOp, Generator_EmptyList_NoOutput) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    mora::ColumnarRelation rel({mora::types::any()}, {});
    rel.append({mora::Value::make_list({})});  // empty list

    auto list_var = pool.intern("L");
    mora::FactPattern fp;
    fp.name    = pool.intern("data");
    fp.negated = false;
    fp.args.push_back(var_expr(list_var));

    auto scan        = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto kw_var      = pool.intern("E");
    auto values_expr = var_expr(list_var);

    auto gen = mora::InClauseOp::build_generator(
        std::move(scan), kw_var, &values_expr, pool, no_syms);

    EXPECT_FALSE(gen->next_chunk().has_value());
}

// Generator form: RHS resolves to non-list (Int) → row is skipped.
TEST(InClauseOp, Generator_NonListRHS_SkipsRow) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    // Relation holds an int, not a list
    mora::ColumnarRelation rel({mora::types::int64()}, {});
    rel.append({mora::Value::make_int(42)});

    auto x_var = pool.intern("X");
    mora::FactPattern fp;
    fp.name    = pool.intern("data");
    fp.negated = false;
    fp.args.push_back(var_expr(x_var));

    auto scan        = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto e_var       = pool.intern("E");
    auto values_expr = var_expr(x_var);  // resolves to Int, not List

    auto gen = mora::InClauseOp::build_generator(
        std::move(scan), e_var, &values_expr, pool, no_syms);

    EXPECT_FALSE(gen->next_chunk().has_value());
}

// ── Membership tests ─────────────────────────────────────────────────────────

// Membership form: one row where var IS in the list → passes; one where not → excluded.
// Relation: (kw_id, kw_list).
// kw_id=0x100 → in list [0x100, 0x200] → passes
// kw_id=0x999 → not in list → excluded
TEST(InClauseOp, Membership_VarInList_Passes) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    mora::ColumnarRelation rel({mora::types::any(), mora::types::any()}, {});
    mora::Value list = mora::Value::make_list({
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0x200),
    });
    rel.append({mora::Value::make_formid(0x100), list});  // kw_id IN list → pass
    rel.append({mora::Value::make_formid(0x999), list});  // kw_id NOT in list → excluded

    auto kw_id_var  = pool.intern("KwId");
    auto kw_list_v  = pool.intern("KwList");
    mora::FactPattern fp;
    fp.name    = pool.intern("kw_data");
    fp.negated = false;
    fp.args.push_back(var_expr(kw_id_var));
    fp.args.push_back(var_expr(kw_list_v));

    auto scan        = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto values_expr = var_expr(kw_list_v);

    auto mem = mora::InClauseOp::build_membership(
        std::move(scan), kw_id_var, &values_expr, pool, no_syms);

    std::vector<uint32_t> out;
    while (auto chunk = mem->next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r)
            out.push_back(chunk->cell(r, 0).as_formid());
    }

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 0x100u);
}

// Membership form: var is not in any list → all rows excluded.
TEST(InClauseOp, Membership_VarNotInList_AllExcluded) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    mora::ColumnarRelation rel({mora::types::any(), mora::types::any()}, {});
    mora::Value list = mora::Value::make_list({
        mora::Value::make_formid(0xAAA),
        mora::Value::make_formid(0xBBB),
    });
    rel.append({mora::Value::make_formid(0x001), list});
    rel.append({mora::Value::make_formid(0x002), list});

    auto kw_id_var  = pool.intern("KwId");
    auto kw_list_v  = pool.intern("KwList");
    mora::FactPattern fp;
    fp.name    = pool.intern("data");
    fp.negated = false;
    fp.args.push_back(var_expr(kw_id_var));
    fp.args.push_back(var_expr(kw_list_v));

    auto scan        = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto values_expr = var_expr(kw_list_v);

    auto mem = mora::InClauseOp::build_membership(
        std::move(scan), kw_id_var, &values_expr, pool, no_syms);

    EXPECT_FALSE(mem->next_chunk().has_value());
}

// Membership form: empty list → all rows excluded.
TEST(InClauseOp, Membership_EmptyList_AllExcluded) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    mora::ColumnarRelation rel({mora::types::any(), mora::types::any()}, {});
    mora::Value empty_list = mora::Value::make_list({});
    rel.append({mora::Value::make_formid(0x001), empty_list});

    auto kw_id_var  = pool.intern("KwId");
    auto kw_list_v  = pool.intern("KwList");
    mora::FactPattern fp;
    fp.name    = pool.intern("data");
    fp.negated = false;
    fp.args.push_back(var_expr(kw_id_var));
    fp.args.push_back(var_expr(kw_list_v));

    auto scan        = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto values_expr = var_expr(kw_list_v);

    auto mem = mora::InClauseOp::build_membership(
        std::move(scan), kw_id_var, &values_expr, pool, no_syms);

    EXPECT_FALSE(mem->next_chunk().has_value());
}

// Membership form preserves output_var_names (same as input).
TEST(InClauseOp, Membership_OutputVarNames_SameAsInput) {
    mora::StringPool pool;
    std::unordered_map<uint32_t, uint32_t> no_syms;

    mora::ColumnarRelation rel({mora::types::any(), mora::types::any()}, {});
    mora::Value list = mora::Value::make_list({mora::Value::make_formid(0x001)});
    rel.append({mora::Value::make_formid(0x001), list});

    auto kw_id_var  = pool.intern("KwId");
    auto kw_list_v  = pool.intern("KwList");
    mora::FactPattern fp;
    fp.name    = pool.intern("data");
    fp.negated = false;
    fp.args.push_back(var_expr(kw_id_var));
    fp.args.push_back(var_expr(kw_list_v));

    auto scan     = mora::ScanOp::build(&rel, fp, pool, no_syms);
    auto expected = scan->output_var_names();
    auto values_expr = var_expr(kw_list_v);

    auto mem = mora::InClauseOp::build_membership(
        std::move(scan), kw_id_var, &values_expr, pool, no_syms);

    auto const& names = mem->output_var_names();
    ASSERT_EQ(names.size(), expected.size());
    for (size_t i = 0; i < names.size(); ++i)
        EXPECT_EQ(names[i].index, expected[i].index);
}

} // namespace
