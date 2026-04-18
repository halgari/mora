// Tests for multi-clause positive-conjunction rule planning (M2).
// Each test seeds small relations in FactDB, evaluates a rule via the
// Evaluator (which tries the vectorized planner first), then asserts
// the output skyrim/set contents or vectorized_rules_count().

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace {

mora::Module parse_and_resolve(mora::StringPool& pool,
                                mora::DiagBag&    diags,
                                const std::string& source)
{
    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    return mod;
}

// ── 1: Two-clause join — canonical shared-var join ─────────────────────
//
// Rule: npc(?npc), base_level(?npc, ?lv) => set gold_value(?npc, ?lv)
// We seed:
//   form/npc:         {0x001}, {0x002}, {0x003}
//   form/base_level:  {0x001, 10}, {0x002, 20}   (0x003 has no level)
// Expected skyrim/set: two rows — (0x001, GoldValue, 10) and (0x002, GoldValue, 20).

TEST(RulePlannerMulti, TwoClauseJoin_SharedVar) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "level_to_gold(NPC, Lv):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Lv)\n"
        "    => set form/gold_value(NPC, Lv)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    auto npc_rel   = pool.intern("npc");
    auto level_rel = pool.intern("base_level");
    db.add_fact(npc_rel,   mora::Tuple{mora::Value::make_formid(0x001)});
    db.add_fact(npc_rel,   mora::Tuple{mora::Value::make_formid(0x002)});
    db.add_fact(npc_rel,   mora::Tuple{mora::Value::make_formid(0x003)});
    db.add_fact(level_rel, mora::Tuple{mora::Value::make_formid(0x001),
                                        mora::Value::make_int(10)});
    db.add_fact(level_rel, mora::Tuple{mora::Value::make_formid(0x002),
                                        mora::Value::make_int(20)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Must have taken the vectorized path.
    EXPECT_GE(eval.vectorized_rules_count(), 1u);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 2u);

    // Collect expected (formid → int) pairs.
    std::vector<std::pair<uint32_t, int64_t>> got;
    for (auto const& t : tuples) {
        EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
        got.push_back({t[0].as_formid(), t[2].as_int()});
    }
    std::sort(got.begin(), got.end());
    ASSERT_EQ(got[0], std::make_pair(uint32_t{0x001}, int64_t{10}));
    ASSERT_EQ(got[1], std::make_pair(uint32_t{0x002}, int64_t{20}));
}

// ── 2: Three-clause chain ──────────────────────────────────────────────
//
// Rule:
//   npc(?npc), base_level(?npc, ?lv), calc_level_min(?npc, ?mn)
//   => set gold_value(?npc, ?mn)
//
// Seed:
//   npc:            {0xA}, {0xB}
//   base_level:     {0xA, 5}, {0xB, 8}
//   calc_level_min: {0xA, 1}, {0xB, 2}
// Expected: two rows in skyrim/set.

TEST(RulePlannerMulti, ThreeClauseChain) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "three_way(NPC, Lv, Mn):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Lv)\n"
        "    form/calc_level_min(NPC, Mn)\n"
        "    => set form/gold_value(NPC, Mn)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    auto npc_rel  = pool.intern("npc");
    auto lv_rel   = pool.intern("base_level");
    auto mn_rel   = pool.intern("calc_level_min");
    db.add_fact(npc_rel, mora::Tuple{mora::Value::make_formid(0xA)});
    db.add_fact(npc_rel, mora::Tuple{mora::Value::make_formid(0xB)});
    db.add_fact(lv_rel,  mora::Tuple{mora::Value::make_formid(0xA), mora::Value::make_int(5)});
    db.add_fact(lv_rel,  mora::Tuple{mora::Value::make_formid(0xB), mora::Value::make_int(8)});
    db.add_fact(mn_rel,  mora::Tuple{mora::Value::make_formid(0xA), mora::Value::make_int(1)});
    db.add_fact(mn_rel,  mora::Tuple{mora::Value::make_formid(0xB), mora::Value::make_int(2)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    EXPECT_GE(eval.vectorized_rules_count(), 1u);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 2u);

    std::vector<std::pair<uint32_t, int64_t>> got;
    for (auto const& t : tuples) got.push_back({t[0].as_formid(), t[2].as_int()});
    std::sort(got.begin(), got.end());
    EXPECT_EQ(got[0].first, 0xAu);
    EXPECT_EQ(got[0].second, 1);
    EXPECT_EQ(got[1].first, 0xBu);
    EXPECT_EQ(got[1].second, 2);
}

// ── 3: No shared var between two clauses → Cartesian → fallback ────────
//
// Rule:  form/npc(NPC), form/weapon(W) => ...
// NPC and W have no shared variable. The planner must decline (Cartesian).
// The tuple fallback runs, which actually produces the cross product —
// we just verify vectorized_rules_count stays 0.

TEST(RulePlannerMulti, CartesianJoin_Rejected_Fallback) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Two unrelated positive patterns — no shared variable.
    std::string const source =
        "cross(NPC, W):\n"
        "    form/npc(NPC)\n"
        "    form/weapon(W)\n"
        "    => set form/gold_value(NPC, 1)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"),    mora::Tuple{mora::Value::make_formid(0x1)});
    db.add_fact(pool.intern("weapon"), mora::Tuple{mora::Value::make_formid(0x2)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Vectorized path must NOT fire for a Cartesian rule.
    EXPECT_EQ(eval.vectorized_rules_count(), 0u);
}

// ── 4: Empty right side — join produces no results ─────────────────────
//
// Rule: npc(?npc), base_level(?npc, ?lv) => set gold_value(?npc, ?lv)
// Seed npc relation with data but leave base_level empty.
// The join should produce 0 output rows — no crash, no incorrect rows.

TEST(RulePlannerMulti, EmptyRightSide_NoResults) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "no_match(NPC, Lv):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Lv)\n"
        "    => set form/gold_value(NPC, Lv)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    // Seed npc but NOT base_level — the join should yield nothing.
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0xDEAD)});
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0xBEEF)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Vectorized path fires (planner succeeds — it doesn't know base_level is empty).
    EXPECT_GE(eval.vectorized_rules_count(), 1u);

    // No output rows.
    auto const* out = db.get_relation_columnar(pool.intern("skyrim/set"));
    // Either the relation doesn't exist yet, or it has 0 rows.
    if (out != nullptr) {
        EXPECT_EQ(out->row_count(), 0u);
    }
}

// ── 5: Duplicate var across two clauses ───────────────────────────────
//
// Rule: base_level(?npc, ?lv), calc_level_min(?npc, ?lv) => set gold_value(?npc, ?lv)
// Both clauses produce (?npc, ?lv), so the join is on both shared vars.
// Only NPCs where base_level == calc_level_min match.

TEST(RulePlannerMulti, SharedVarAcrossTwoClauses_TwoSharedVars) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "eq_level(NPC, Lv):\n"
        "    form/base_level(NPC, Lv)\n"
        "    form/calc_level_min(NPC, Lv)\n"
        "    => set form/gold_value(NPC, Lv)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    auto lv_rel = pool.intern("base_level");
    auto mn_rel = pool.intern("calc_level_min");
    // 0xA: base_level=10, calc_level_min=10 → match
    // 0xB: base_level=8,  calc_level_min=5  → no match
    db.add_fact(lv_rel, mora::Tuple{mora::Value::make_formid(0xA), mora::Value::make_int(10)});
    db.add_fact(lv_rel, mora::Tuple{mora::Value::make_formid(0xB), mora::Value::make_int(8)});
    db.add_fact(mn_rel, mora::Tuple{mora::Value::make_formid(0xA), mora::Value::make_int(10)});
    db.add_fact(mn_rel, mora::Tuple{mora::Value::make_formid(0xB), mora::Value::make_int(5)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    EXPECT_GE(eval.vectorized_rules_count(), 1u);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    EXPECT_EQ(tuples[0][0].as_formid(), 0xAu);
    EXPECT_EQ(tuples[0][2].as_int(), 10);
}

// ── 6: Duplicate var within single clause (ScanOp eq-filter) + join ───
//
// Rule: base_level(?npc, ?npc) — semantically odd (NPC formid equals int
// level), but exercises ScanOp's eq-filter path, then joins on ?npc.
// Actually that produces zero matches for sane data; instead test
// duplicate var in one clause that does match:
//
// Form/base_level has 2-arg arity (FormRef, Int). To exercise the eq-filter
// we need a relation where the same var appears twice. Use a derived or seeded
// relation. Let's seed our own:
//
// Rule: npc(?npc), base_level(?npc, ?lv) => set gold_value(?npc, 99)
// where the npc scan also filters to only npc == 0x001.
// Actually, the simpler path is: test that the single-clause eq-filter still
// works in combination with a join.
//
// Strategy: rule uses base_level(NPC, NPC) — pattern where both positions
// must hold the same value. In practice, NPC positions are FormRef and level
// is Int, so no row matches. We just want to ensure no crash and zero output.

TEST(RulePlannerMulti, DuplicateVarWithinClause_EqFilter) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // base_level(?npc, ?npc): eq-filter between col0 and col1.
    // Types differ so no row will match — that's fine, we're testing stability.
    std::string const source =
        "weird_eq(NPC):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, NPC)\n"
        "    => set form/gold_value(NPC, 0)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"),        mora::Tuple{mora::Value::make_formid(0x1)});
    db.add_fact(pool.intern("base_level"), mora::Tuple{mora::Value::make_formid(0x1),
                                                        mora::Value::make_int(1)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    EXPECT_GE(eval.vectorized_rules_count(), 1u);

    // No matching rows (FormID != Int for any row in base_level).
    auto const* out = db.get_relation_columnar(pool.intern("skyrim/set"));
    if (out != nullptr) {
        EXPECT_EQ(out->row_count(), 0u);
    }
}

// ── 7: Two-clause join — assert vectorized_rules_count > 0 explicitly ─
//
// Same two-clause setup as test 1 but focused on asserting the path taken.

TEST(RulePlannerMulti, TwoClause_VectorizedPathFires) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "check_path(NPC, Lv):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Lv)\n"
        "    => set form/gold_value(NPC, Lv)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"),
                mora::Tuple{mora::Value::make_formid(0x42)});
    db.add_fact(pool.intern("base_level"),
                mora::Tuple{mora::Value::make_formid(0x42), mora::Value::make_int(7)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    EXPECT_GE(eval.vectorized_rules_count(), 1u);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    EXPECT_EQ(tuples[0][0].as_formid(), 0x42u);
    EXPECT_EQ(tuples[0][2].as_int(), 7);
}

} // namespace
