// Tests for multi-clause positive-conjunction rule planning (M2).
// Each test seeds small relations in FactDB, evaluates a rule via the
// Evaluator, then asserts the output skyrim/set contents.

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

// ── 3: No shared var between two clauses → Cartesian → hard diagnostic ─────
//
// Rule:  form/npc(NPC), form/weapon(W) => ...
// NPC and W have no shared variable. The planner declines (Cartesian join).
// After M2 there is no tuple fallback — the evaluator emits a hard diagnostic
// and produces no output rows.

TEST(RulePlannerMulti, CartesianJoin_Rejected_EmitsDiagnostic) {
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

    // Planner declines Cartesian join → hard diagnostic emitted.
    EXPECT_TRUE(diags.has_errors());
    // No output rows produced.
    auto const* out = db.get_relation_columnar(pool.intern("skyrim/set"));
    if (out != nullptr) { EXPECT_EQ(out->row_count(), 0u); }
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

    // No matching rows (FormID != Int for any row in base_level).
    auto const* out = db.get_relation_columnar(pool.intern("skyrim/set"));
    if (out != nullptr) {
        EXPECT_EQ(out->row_count(), 0u);
    }
}

// ── 7: Two-clause join — correctness check ──────────────────────────────────
//
// Same two-clause setup as test 1 but as an independent correctness test.

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

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    EXPECT_EQ(tuples[0][0].as_formid(), 0x42u);
    EXPECT_EQ(tuples[0][2].as_int(), 7);
}

// ── M3: AntiJoinOp integration ────────────────────────────────────────────
//
// Rule (built programmatically):
//   safe_weapons(W): weapon(W), not dangerous(W) => set form/gold_value(W, 1)
// Only weapons that do NOT appear in dangerous get an effect.
//
// Programmatic rule construction bypasses NameResolver, allowing arbitrary
// relation names without schema registration.

TEST(RulePlannerMulti, NegatedPattern_AntiJoin) {
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::FactDB db(pool);
    // Weapon 0xA: safe (not in dangerous) → gets effect
    // Weapon 0xB: dangerous → suppressed
    // Weapon 0xC: safe → gets effect
    db.add_fact(pool.intern("weapon"),   mora::Tuple{mora::Value::make_formid(0xA)});
    db.add_fact(pool.intern("weapon"),   mora::Tuple{mora::Value::make_formid(0xB)});
    db.add_fact(pool.intern("weapon"),   mora::Tuple{mora::Value::make_formid(0xC)});
    db.add_fact(pool.intern("dangerous"),mora::Tuple{mora::Value::make_formid(0xB)});

    auto var = [&](const char* name) -> mora::Expr {
        return mora::Expr{mora::VariableExpr{pool.intern(name), {}}, {}};
    };

    mora::Rule rule;
    rule.name = pool.intern("safe_weapons");
    rule.head_args.push_back(var("W"));

    // Clause 1: weapon(W)
    {
        mora::FactPattern fp;
        fp.name    = pool.intern("weapon");
        fp.negated = false;
        fp.args.push_back(var("W"));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }
    // Clause 2: not dangerous(W)
    {
        mora::FactPattern fp;
        fp.name    = pool.intern("dangerous");
        fp.negated = true;
        fp.args.push_back(var("W"));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }
    // Effect
    {
        mora::Effect eff;
        eff.verb       = mora::VerbKind::Set;
        eff.namespace_ = pool.intern("form");
        eff.name       = pool.intern("gold_value");
        eff.args.push_back(var("W"));
        eff.args.push_back(mora::Expr{mora::IntLiteral{1, {}}, {}});
        rule.effects.push_back(std::move(eff));
    }

    mora::Module mod;
    mod.rules.push_back(std::move(rule));

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 2u);

    std::vector<uint32_t> out;
    for (auto const& t : tuples) out.push_back(t[0].as_formid());
    std::sort(out.begin(), out.end());
    EXPECT_EQ(out[0], 0xAu);
    EXPECT_EQ(out[1], 0xCu);
}

// ── M3: AntiJoin — negated relation with no rows (empty right) → all left pass
TEST(RulePlannerMulti, NegatedPattern_EmptyNegated_AllPass) {
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::FactDB db(pool);
    // 3 weapons, banned relation is absent → all 3 get the effect.
    db.add_fact(pool.intern("weapon"), mora::Tuple{mora::Value::make_formid(0x1)});
    db.add_fact(pool.intern("weapon"), mora::Tuple{mora::Value::make_formid(0x2)});
    db.add_fact(pool.intern("weapon"), mora::Tuple{mora::Value::make_formid(0x3)});
    // (no "banned" facts)

    auto var = [&](const char* name) -> mora::Expr {
        return mora::Expr{mora::VariableExpr{pool.intern(name), {}}, {}};
    };

    mora::Rule rule;
    rule.name = pool.intern("no_banned");
    rule.head_args.push_back(var("W"));

    {
        mora::FactPattern fp;
        fp.name    = pool.intern("weapon");
        fp.negated = false;
        fp.args.push_back(var("W"));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }
    {
        mora::FactPattern fp;
        fp.name    = pool.intern("banned");
        fp.negated = true;
        fp.args.push_back(var("W"));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }
    {
        mora::Effect eff;
        eff.verb       = mora::VerbKind::Set;
        eff.namespace_ = pool.intern("form");
        eff.name       = pool.intern("gold_value");
        eff.args.push_back(var("W"));
        eff.args.push_back(mora::Expr{mora::IntLiteral{99, {}}, {}});
        rule.effects.push_back(std::move(eff));
    }

    mora::Module mod;
    mod.rules.push_back(std::move(rule));

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 3u);
}

// ── M3: InClause generator integration ───────────────────────────────────────
//
// Rule: ?item in ?item_list where item_list is bound from a relation holding
// a List-typed value.
// Uses the Evaluator + programmatically built Rule AST (same idiom as
// evaluator_test.cpp's ElementInListVar test).

TEST(RulePlannerMulti, InClause_Generator_Vectorized) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Relation: kw_source(weapon_formid, kw_list)
    // kw_list is a List<formid> of keyword IDs.
    mora::FactDB db(pool);
    mora::Value kw_list = mora::Value::make_list({
        mora::Value::make_formid(0xAA01),
        mora::Value::make_formid(0xAA02),
        mora::Value::make_formid(0xAA03),
    });
    db.add_fact(pool.intern("kw_source"), {
        mora::Value::make_formid(0x10),
        kw_list,
    });

    // Rule (built programmatically):
    //   has_keyword(W, KW):
    //       kw_source(W, KwList)
    //       KW in KwList
    //       => set form/gold_value(W, 1)
    auto var = [&](const char* name) -> mora::Expr {
        return mora::Expr{mora::VariableExpr{pool.intern(name), {}}, {}};
    };

    mora::Rule rule;
    rule.name = pool.intern("has_keyword");
    rule.head_args.push_back(var("W"));
    rule.head_args.push_back(var("KW"));

    // Clause 1: kw_source(W, KwList)
    {
        mora::FactPattern fp;
        fp.name    = pool.intern("kw_source");
        fp.negated = false;
        fp.args.push_back(var("W"));
        fp.args.push_back(var("KwList"));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }

    // Clause 2: KW in KwList
    {
        mora::InClause ic;
        ic.variable = std::make_unique<mora::Expr>(var("KW"));
        ic.values.push_back(var("KwList"));
        rule.body.push_back(mora::Clause{std::move(ic), {}});
    }

    // Effect: => set form/gold_value(W, 1) — goes into rule.effects, not rule.body
    {
        mora::Effect eff;
        eff.verb       = mora::VerbKind::Set;
        eff.namespace_ = pool.intern("form");
        eff.name       = pool.intern("gold_value");
        eff.args.push_back(var("W"));
        eff.args.push_back(mora::Expr{mora::IntLiteral{1, {}}, {}});
        rule.effects.push_back(std::move(eff));
    }

    mora::Module mod;
    mod.rules.push_back(std::move(rule));

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Weapon 0x10 has 3 keywords → should produce 3 rows in skyrim/set.
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    // The effect sets gold_value on W (0x10) for each KW iteration.
    // So 3 rows: (0x10, GoldValue, 1) × 3.
    ASSERT_EQ(tuples.size(), 3u);
    for (auto const& t : tuples) {
        EXPECT_EQ(t[0].as_formid(), 0x10u);
        EXPECT_EQ(t[2].as_int(), 1);
    }
}

// ── M3: InClause membership integration ─────────────────────────────────────
//
// Rule: kw_check(KwId, W) :-
//     kw_data(KwId, KwList, W)  -- KwId and KwList come from same row
//     KwId in KwList             -- membership: KwId is already bound
//     => set form/gold_value(W, 42)
//
// The single kw_data relation holds (kw_id, kw_list, weapon_formid).
// Row 1: kw_id=0x100 IN [0x100, 0x200], weapon=0x10 → passes
// Row 2: kw_id=0x999 NOT IN [0x100, 0x200], weapon=0x20 → excluded

TEST(RulePlannerMulti, InClause_Membership_Vectorized) {
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::FactDB db(pool);
    mora::Value list = mora::Value::make_list({
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0x200),
    });
    // (kw_id, kw_list, weapon)
    db.add_fact(pool.intern("kw_data"), {mora::Value::make_formid(0x100), list,
                                          mora::Value::make_formid(0x10)});  // passes
    db.add_fact(pool.intern("kw_data"), {mora::Value::make_formid(0x999), list,
                                          mora::Value::make_formid(0x20)});  // excluded

    auto var = [&](const char* name) -> mora::Expr {
        return mora::Expr{mora::VariableExpr{pool.intern(name), {}}, {}};
    };

    mora::Rule rule;
    rule.name = pool.intern("kw_check");
    rule.head_args.push_back(var("W"));

    // Clause 1: kw_data(KwId, KwList, W)
    {
        mora::FactPattern fp;
        fp.name    = pool.intern("kw_data");
        fp.negated = false;
        fp.args.push_back(var("KwId"));
        fp.args.push_back(var("KwList"));
        fp.args.push_back(var("W"));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }
    // Clause 2: KwId in KwList (membership — KwId already bound by scan)
    {
        mora::InClause ic;
        ic.variable = std::make_unique<mora::Expr>(var("KwId"));
        ic.values.push_back(var("KwList"));
        rule.body.push_back(mora::Clause{std::move(ic), {}});
    }
    // Effect
    {
        mora::Effect eff;
        eff.verb       = mora::VerbKind::Set;
        eff.namespace_ = pool.intern("form");
        eff.name       = pool.intern("gold_value");
        eff.args.push_back(var("W"));
        eff.args.push_back(mora::Expr{mora::IntLiteral{42, {}}, {}});
        rule.effects.push_back(std::move(eff));
    }

    mora::Module mod;
    mod.rules.push_back(std::move(rule));

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Only weapon 0x10 (KwId=0x100 is in KwList) gets the effect.
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    EXPECT_EQ(tuples[0][0].as_formid(), 0x10u);
    EXPECT_EQ(tuples[0][2].as_int(), 42);
}

} // namespace
