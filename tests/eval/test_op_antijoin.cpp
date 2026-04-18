// Plan 14 M3 — AntiJoinOp unit tests.
// Tests AntiJoinOp in isolation by constructing two ScanOp sources and
// verifying that only left rows with no right-side match are emitted.

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"
#include "mora/eval/op_antijoin.h"
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

mora::FactPattern make_pattern_1(mora::StringId rel, mora::Expr a,
                                   bool negated = false) {
    mora::FactPattern fp;
    fp.name    = rel;
    fp.negated = negated;
    fp.args.push_back(std::move(a));
    return fp;
}

// Build a ScanOp over a single-column ColumnarRelation.
// Column type: types::any() — holds FormIDs or ints interchangeably.
std::unique_ptr<mora::Operator> make_scan_1col(
    mora::StringPool& pool,
    const std::string& rel_name,
    const std::string& var_name,
    const mora::ColumnarRelation& rel)
{
    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto fp = make_pattern_1(pool.intern(rel_name),
                              var_expr(pool.intern(var_name)));
    return mora::ScanOp::build(&rel, fp, pool, no_syms);
}

// ── Tests ────────────────────────────────────────────────────────────────────

// Empty right side: every left row survives.
TEST(AntiJoinOp, EmptyRight_AllLeftPass) {
    mora::StringPool pool;

    // Left: 3 FormIDs
    mora::ColumnarRelation left_rel({mora::types::any()}, {});
    left_rel.append({mora::Value::make_formid(0x1)});
    left_rel.append({mora::Value::make_formid(0x2)});
    left_rel.append({mora::Value::make_formid(0x3)});

    // Right: empty
    mora::ColumnarRelation right_rel({mora::types::any()}, {});

    auto left  = make_scan_1col(pool, "left",  "w", left_rel);
    auto right = make_scan_1col(pool, "right", "w", right_rel);

    auto shared = std::vector<mora::StringId>{pool.intern("w")};
    mora::AntiJoinOp anti(std::move(left), std::move(right), shared);

    std::vector<uint32_t> out;
    while (auto chunk = anti.next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r)
            out.push_back(chunk->cell(r, 0).as_formid());
    }

    ASSERT_EQ(out.size(), 3u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0], 0x1u);
    EXPECT_EQ(out[1], 0x2u);
    EXPECT_EQ(out[2], 0x3u);
}

// Right matches one left row: that row is excluded, others pass.
TEST(AntiJoinOp, OneMatch_MatchedRowExcluded) {
    mora::StringPool pool;

    // Left: FormIDs 0x1, 0x2, 0x3
    mora::ColumnarRelation left_rel({mora::types::any()}, {});
    left_rel.append({mora::Value::make_formid(0x1)});
    left_rel.append({mora::Value::make_formid(0x2)});
    left_rel.append({mora::Value::make_formid(0x3)});

    // Right: only 0x2
    mora::ColumnarRelation right_rel({mora::types::any()}, {});
    right_rel.append({mora::Value::make_formid(0x2)});

    auto left  = make_scan_1col(pool, "left",  "w", left_rel);
    auto right = make_scan_1col(pool, "right", "w", right_rel);

    auto shared = std::vector<mora::StringId>{pool.intern("w")};
    mora::AntiJoinOp anti(std::move(left), std::move(right), shared);

    std::vector<uint32_t> out;
    while (auto chunk = anti.next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r)
            out.push_back(chunk->cell(r, 0).as_formid());
    }

    // 0x2 should be excluded
    ASSERT_EQ(out.size(), 2u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0], 0x1u);
    EXPECT_EQ(out[1], 0x3u);
}

// Right matches all left rows: output is empty.
TEST(AntiJoinOp, AllMatched_EmptyOutput) {
    mora::StringPool pool;

    mora::ColumnarRelation left_rel({mora::types::any()}, {});
    left_rel.append({mora::Value::make_formid(0xA)});
    left_rel.append({mora::Value::make_formid(0xB)});

    mora::ColumnarRelation right_rel({mora::types::any()}, {});
    right_rel.append({mora::Value::make_formid(0xA)});
    right_rel.append({mora::Value::make_formid(0xB)});

    auto left  = make_scan_1col(pool, "l", "x", left_rel);
    auto right = make_scan_1col(pool, "r", "x", right_rel);

    auto shared = std::vector<mora::StringId>{pool.intern("x")};
    mora::AntiJoinOp anti(std::move(left), std::move(right), shared);

    EXPECT_FALSE(anti.next_chunk().has_value());
}

// Collision handling: right has many rows with the same hash but only one real
// match. Only the truly matching row is excluded.
TEST(AntiJoinOp, HashCollisionSafety_OnlyExactMatchExcluded) {
    mora::StringPool pool;

    // Seed left with FormIDs 1..5
    mora::ColumnarRelation left_rel({mora::types::any()}, {});
    for (uint32_t i = 1; i <= 5; ++i)
        left_rel.append({mora::Value::make_formid(i)});

    // Right has only FormID 3
    mora::ColumnarRelation right_rel({mora::types::any()}, {});
    right_rel.append({mora::Value::make_formid(3)});

    auto left  = make_scan_1col(pool, "l", "v", left_rel);
    auto right = make_scan_1col(pool, "r", "v", right_rel);

    auto shared = std::vector<mora::StringId>{pool.intern("v")};
    mora::AntiJoinOp anti(std::move(left), std::move(right), shared);

    std::vector<uint32_t> out;
    while (auto chunk = anti.next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r)
            out.push_back(chunk->cell(r, 0).as_formid());
    }

    // 3 should be excluded, rest should appear
    ASSERT_EQ(out.size(), 4u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0], 1u);
    EXPECT_EQ(out[1], 2u);
    EXPECT_EQ(out[2], 4u);
    EXPECT_EQ(out[3], 5u);
}

// output_var_names() returns left's var names (right adds nothing).
TEST(AntiJoinOp, OutputVarNames_MatchLeft) {
    mora::StringPool pool;

    mora::ColumnarRelation left_rel({mora::types::any()}, {});
    left_rel.append({mora::Value::make_formid(0x1)});

    mora::ColumnarRelation right_rel({mora::types::any()}, {});
    right_rel.append({mora::Value::make_formid(0x99)});

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto fp_l = make_pattern_1(pool.intern("l"), var_expr(pool.intern("weapon")));
    auto fp_r = make_pattern_1(pool.intern("r"), var_expr(pool.intern("weapon")));
    auto left  = mora::ScanOp::build(&left_rel,  fp_l, pool, no_syms);
    auto right = mora::ScanOp::build(&right_rel, fp_r, pool, no_syms);

    auto expected_names = left->output_var_names();
    auto shared = std::vector<mora::StringId>{pool.intern("weapon")};
    mora::AntiJoinOp anti(std::move(left), std::move(right), shared);

    auto const& names = anti.output_var_names();
    ASSERT_EQ(names.size(), expected_names.size());
    for (size_t i = 0; i < names.size(); ++i)
        EXPECT_EQ(names[i].index, expected_names[i].index);
}

// Two-column left, one shared var: anti-join on ?w, left also has ?lv column.
// Output preserves both left columns.
TEST(AntiJoinOp, TwoColumnLeft_BothColumnsPreserved) {
    mora::StringPool pool;

    // left relation: (weapon_formid, level_int)
    mora::ColumnarRelation left_rel({mora::types::any(), mora::types::int64()}, {});
    left_rel.append({mora::Value::make_formid(0x10), mora::Value::make_int(5)});
    left_rel.append({mora::Value::make_formid(0x20), mora::Value::make_int(10)});
    left_rel.append({mora::Value::make_formid(0x30), mora::Value::make_int(15)});

    // right relation: (weapon_formid) — marks 0x20 as "dangerous"
    mora::ColumnarRelation right_rel({mora::types::any()}, {});
    right_rel.append({mora::Value::make_formid(0x20)});

    std::unordered_map<uint32_t, uint32_t> no_syms;
    auto w_id  = pool.intern("w");
    auto lv_id = pool.intern("lv");

    mora::FactPattern fp_l;
    fp_l.name    = pool.intern("left_rel");
    fp_l.negated = false;
    fp_l.args.push_back(var_expr(w_id));
    fp_l.args.push_back(var_expr(lv_id));

    mora::FactPattern fp_r;
    fp_r.name    = pool.intern("right_rel");
    fp_r.negated = false;
    fp_r.args.push_back(var_expr(w_id));

    auto left  = mora::ScanOp::build(&left_rel,  fp_l, pool, no_syms);
    auto right = mora::ScanOp::build(&right_rel, fp_r, pool, no_syms);

    auto shared = std::vector<mora::StringId>{w_id};
    mora::AntiJoinOp anti(std::move(left), std::move(right), shared);

    // Collect output: (formid, level)
    std::vector<std::pair<uint32_t, int64_t>> out;
    while (auto chunk = anti.next_chunk()) {
        for (size_t r = 0; r < chunk->row_count(); ++r) {
            out.push_back({chunk->cell(r, 0).as_formid(),
                           chunk->cell(r, 1).as_int()});
        }
    }

    // 0x20 is excluded; 0x10 and 0x30 survive with their level columns intact.
    ASSERT_EQ(out.size(), 2u);
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0].first,  0x10u);
    EXPECT_EQ(out[0].second, 5);
    EXPECT_EQ(out[1].first,  0x30u);
    EXPECT_EQ(out[1].second, 15);
}

} // namespace
