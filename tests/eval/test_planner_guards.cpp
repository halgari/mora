// Plan 14 M2 — Planner guard and conditional-effect integration tests.
// Covers: GuardClause in rule bodies via FilterOp, multiple guards in
// the same rule. All effects use qualified rule heads + DerivedAppendOp.

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

// ── Guard in body: only rows passing the guard get an effect ─────────────

TEST(PlannerGuards, GuardInBody_FiltersRows) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Rule: skyrim/set(NPC, :GoldValue, 500) :- npc(NPC), base_level(NPC, Level), Level >= 10
    std::string const source =
        "skyrim/set(NPC, :GoldValue, 500):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Level)\n"
        "    Level >= 10\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    // NPC 0x1: level 5 — fails guard.
    // NPC 0x2: level 15 — passes guard.
    // NPC 0x3: level 20 — passes guard.
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0x1)});
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0x2)});
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0x3)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x1), mora::Value::make_int(5)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x2), mora::Value::make_int(15)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x3), mora::Value::make_int(20)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Only NPCs 0x2 and 0x3 (level >= 10) get gold.
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 2u);

    std::vector<uint32_t> fids;
    for (auto const& t : tuples) {
        EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
        EXPECT_EQ(t[2].as_int(), 500);
        fids.push_back(t[0].as_formid());
    }
    std::sort(fids.begin(), fids.end());
    EXPECT_EQ(fids[0], 0x2u);
    EXPECT_EQ(fids[1], 0x3u);
}

// ── Multiple guards: both must pass ──────────────────────────────────────

TEST(PlannerGuards, MultipleGuards_BothMustPass) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Rule with two guards: Level >= 10 AND Level <= 20.
    // Only NPCs in the 10-20 band get gold.
    std::string const source =
        "skyrim/set(NPC, :GoldValue, 100):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Level)\n"
        "    Level >= 10\n"
        "    Level <= 20\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0xA)});  // level 5 — both fail
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0xB)});  // level 15 — both pass
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0xC)});  // level 30 — second fails
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0xA), mora::Value::make_int(5)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0xB), mora::Value::make_int(15)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0xC), mora::Value::make_int(30)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Only NPC 0xB (level=15) is in [10,20].
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    EXPECT_EQ(tuples[0][0].as_formid(), 0xBu);
    EXPECT_EQ(tuples[0][2].as_int(), 100);
}

// ── Guard with no matching rows → empty effect output ───────────────────

TEST(PlannerGuards, GuardFiltersAllRows_EmptyOutput) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Guard that nothing can pass: Level > 1000.
    std::string const source =
        "skyrim/set(NPC, :GoldValue, 1):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Level)\n"
        "    Level > 1000\n";

    auto mod = parse_and_resolve(pool, diags, source);
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0x5)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x5), mora::Value::make_int(50)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // No rows should be emitted.
    auto const* set_rel = db.get_relation_columnar(pool.intern("skyrim/set"));
    if (set_rel) { EXPECT_EQ(set_rel->row_count(), 0u); }
}

// ── Guard in body filters rows (replaces old ConditionalEffect) ──────────
//
// The conditional effect pattern moves the guard into the rule body.

TEST(PlannerGuards, ConditionalEffect_VectorizedPath) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Rule: scans all NPCs + their levels; guard Level >= 10 in body.
    std::string const source =
        "skyrim/set(NPC, :GoldValue, 777):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Level)\n"
        "    Level >= 10\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    // NPC 0x1: level 5 — conditional guard fails.
    // NPC 0x2: level 20 — conditional guard passes.
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0x1)});
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0x2)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x1), mora::Value::make_int(5)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x2), mora::Value::make_int(20)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Only NPC 0x2 passes the conditional guard.
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    EXPECT_EQ(tuples[0][0].as_formid(), 0x2u);
    EXPECT_EQ(pool.get(tuples[0][1].as_keyword()), "GoldValue");
    EXPECT_EQ(tuples[0][2].as_int(), 777);
}

// ── Two qualified-head rules: one filtered, one unconditional ─────────────

TEST(PlannerGuards, ConditionalAndUnconditional_BothFire) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Rule 1: all NPCs get :GoldValue = 1 (unconditional).
    // Rule 2: only high-level NPCs get :Damage = 99 (guarded).
    std::string const source =
        "skyrim/set(NPC, :GoldValue, 1):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Level)\n"
        "skyrim/set(NPC, :Damage, 99):\n"
        "    form/npc(NPC)\n"
        "    form/base_level(NPC, Level)\n"
        "    Level >= 15\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    // NPC 0xA: level 10 — gets gold, no damage bonus.
    // NPC 0xB: level 20 — gets both gold and damage bonus.
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0xA)});
    db.add_fact(pool.intern("npc"),        {mora::Value::make_formid(0xB)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0xA), mora::Value::make_int(10)});
    db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0xB), mora::Value::make_int(20)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Both NPCs get gold (unconditional).
    auto const& set_tuples = db.get_relation(pool.intern("skyrim/set"));
    // 2 gold-value rows + 1 damage row = 3 total in skyrim/set.
    ASSERT_EQ(set_tuples.size(), 3u);

    // Count rows per field.
    int gold_count = 0;
    int damage_count = 0;
    for (auto const& t : set_tuples) {
        std::string field(pool.get(t[1].as_keyword()));
        if (field == "GoldValue") ++gold_count;
        if (field == "Damage")    ++damage_count;
    }
    EXPECT_EQ(gold_count,   2);  // both NPCs
    EXPECT_EQ(damage_count, 1);  // only 0xB
}

} // namespace
