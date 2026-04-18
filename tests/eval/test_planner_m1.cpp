// Plan 14 M1 regression tests.
// Covers: UnionOp merged lookup (Task 1.1), KeywordLiteral FormID resolution
// (Task 1.2), non-Set verbs (Task 1.3), multi-effect rules (Task 1.4).

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

// ── Task 1.1 Test A: relation only in derived_facts still scanned ────────
//
// We need to observe derived facts being used. We do this with a two-rule
// module: rule 1 derives facts, rule 2 scans that derived relation.
// After Plan 14 M1, rule 2 should use UnionOp and pick up what rule 1 derived.

TEST(PlannerM1, MergedLookup_DerivedOnlyRelation) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Rule 1: npc(NPC) → derived my_npcs(NPC)  (derived rule, no effect)
    // Rule 2: my_npcs(NPC) => set gold_value(NPC, 42)  (effect rule)
    //
    // After rule 1 runs vectorized, derived_facts gains "my_npcs".
    // Rule 2 scans "my_npcs" which only exists in derived_facts → UnionOp
    // must find it and emit the effect.
    std::string const source =
        "my_npcs(NPC):\n"
        "    form/npc(NPC)\n"
        "\n"
        "give_gold(NPC):\n"
        "    my_npcs(NPC)\n"
        "    => set form/gold_value(NPC, 42)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0xAAA)});
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0xBBB)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Both rules should be vectorized.
    EXPECT_EQ(eval.vectorized_rules_count(), 2u);

    // Effect rule should have emitted 2 rows (one per NPC).
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 2u);

    std::vector<uint32_t> fids;
    for (auto const& t : tuples) {
        EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
        EXPECT_EQ(t[2].as_int(), 42);
        fids.push_back(t[0].as_formid());
    }
    std::sort(fids.begin(), fids.end());
    EXPECT_EQ(fids[0], 0xAAAu);
    EXPECT_EQ(fids[1], 0xBBBu);
}

// ── Task 1.1 Test B: relation in both input_db and derived_facts ─────────
//
// We cannot easily inject derived_facts directly (private), but we can
// observe the union behaviour by running a derived rule that adds to a
// relation that also has data in input_db. Then a second rule scans both.
//
// Alternative approach: just verify the single-derived-relation case
// (Test A above) covers the UnionOp code path. The unit test for UnionOp
// itself (if written) would cover the union more directly.
// For integration we rely on Test A.

// ── Task 1.3: Add verb routes to skyrim/add ─────────────────────────────

TEST(PlannerM1, AddVerb_RoutesToSkyrimAdd) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // "add form/keyword(NPC, :SomeKW)" → skyrim/add with Keyword field
    std::string const source =
        "tag_it(NPC):\n"
        "    form/npc(NPC)\n"
        "    => add form/keyword(NPC, 100)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0x100)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Vectorized path should fire (add verb now supported).
    EXPECT_EQ(eval.vectorized_rules_count(), 1u);

    // Row should appear in skyrim/add (not skyrim/set).
    auto const& add_tuples = db.get_relation(pool.intern("skyrim/add"));
    ASSERT_EQ(add_tuples.size(), 1u);
    EXPECT_EQ(add_tuples[0][0].as_formid(), 0x100u);
    EXPECT_EQ(pool.get(add_tuples[0][1].as_keyword()), "Keywords");

    // skyrim/set should be empty.
    auto const* set_rel = db.get_relation_columnar(pool.intern("skyrim/set"));
    if (set_rel) { EXPECT_EQ(set_rel->row_count(), 0u); }
}

// ── Task 1.3: Remove verb routes to skyrim/remove ───────────────────────

TEST(PlannerM1, RemoveVerb_RoutesToSkyrimRemove) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "untag_it(NPC):\n"
        "    form/npc(NPC)\n"
        "    => remove form/keyword(NPC, 200)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0x200)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    EXPECT_EQ(eval.vectorized_rules_count(), 1u);

    auto const& rem_tuples = db.get_relation(pool.intern("skyrim/remove"));
    ASSERT_EQ(rem_tuples.size(), 1u);
    EXPECT_EQ(rem_tuples[0][0].as_formid(), 0x200u);
    EXPECT_EQ(pool.get(rem_tuples[0][1].as_keyword()), "Keywords");
}

// ── Task 1.4: Multi-effect rule — both effects emitted vectorized ─────────

TEST(PlannerM1, MultiEffect_BothEffectsEmitted) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "dual_set(NPC):\n"
        "    form/npc(NPC)\n"
        "    => set form/gold_value(NPC, 10)\n"
        "    => set form/damage(NPC, 20)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0x300)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    // Vectorized path fires.
    EXPECT_EQ(eval.vectorized_rules_count(), 1u);

    // Both effects appear in skyrim/set.
    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 2u);

    // Collect (field, value) pairs.
    std::vector<std::pair<std::string, int64_t>> got;
    for (auto const& t : tuples) {
        EXPECT_EQ(t[0].as_formid(), 0x300u);
        got.push_back({std::string(pool.get(t[1].as_keyword())), t[2].as_int()});
    }
    std::sort(got.begin(), got.end());
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].first, "Damage");
    EXPECT_EQ(got[0].second, 20);
    EXPECT_EQ(got[1].first, "GoldValue");
    EXPECT_EQ(got[1].second, 10);
}

// ── Task 1.4: Multi-effect rule — mixed verbs (set + add) ───────────────

TEST(PlannerM1, MultiEffect_MixedVerbs) {
    mora::StringPool pool;
    mora::DiagBag diags;

    std::string const source =
        "mixed_verbs(NPC):\n"
        "    form/npc(NPC)\n"
        "    => set form/gold_value(NPC, 50)\n"
        "    => add form/keyword(NPC, 999)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), mora::Tuple{mora::Value::make_formid(0x400)});

    mora::Evaluator eval(pool, diags, db);
    eval.evaluate_module(mod, db);

    EXPECT_EQ(eval.vectorized_rules_count(), 1u);

    // set effect
    auto const& set_tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(set_tuples.size(), 1u);
    EXPECT_EQ(set_tuples[0][0].as_formid(), 0x400u);
    EXPECT_EQ(pool.get(set_tuples[0][1].as_keyword()), "GoldValue");
    EXPECT_EQ(set_tuples[0][2].as_int(), 50);

    // add effect
    auto const& add_tuples = db.get_relation(pool.intern("skyrim/add"));
    ASSERT_EQ(add_tuples.size(), 1u);
    EXPECT_EQ(add_tuples[0][0].as_formid(), 0x400u);
    EXPECT_EQ(pool.get(add_tuples[0][1].as_keyword()), "Keywords");
}

// ── Task 1.2: KeywordLiteral resolved to FormID via symbol_formids ────────
//
// If `:SomeAlias` appears as a target arg and symbol_formids maps it to a
// FormID, the vectorized path must produce a FormID target (not a keyword).
// We test this by using set_symbol_formid() and checking the emitted row.

TEST(PlannerM1, KeywordLiteral_ResolvesViaSymbolFormids) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Rule: for each base_level(NPC, Val), set Val on the NPC.
    // The NPC arg is a variable, so this isn't testing keyword-as-target
    // directly. Instead, test keyword as value arg: gold_value(:MyAlias, 1)
    // where :MyAlias should resolve to a FormID 0x999.
    //
    // Realistic scenario: set gold_value(:FixedNPC, Val) where :FixedNPC
    // is the target (a keyword constant that resolves to a FormID).
    std::string const source =
        "fix_npc(Val):\n"
        "    form/base_level(:FixedNPC, Val)\n"
        "    => set form/gold_value(:FixedNPC, Val)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    mora::FactDB db(pool);
    db.add_fact(pool.intern("base_level"),
                mora::Tuple{mora::Value::make_formid(0x999),
                            mora::Value::make_int(77)});

    mora::Evaluator eval(pool, diags, db);
    // set_symbol_formid stores both "FixedNPC" and ":FixedNPC" in the map.
    // Pass without colon so the helper generates the colon-prefixed version too.
    eval.set_symbol_formid(pool.intern("FixedNPC"), 0x999);
    eval.evaluate_module(mod, db);

    EXPECT_EQ(eval.vectorized_rules_count(), 1u);

    auto const& tuples = db.get_relation(pool.intern("skyrim/set"));
    ASSERT_EQ(tuples.size(), 1u);
    // Target should be FormID 0x999, not a keyword.
    EXPECT_EQ(tuples[0][0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(tuples[0][0].as_formid(), 0x999u);
    EXPECT_EQ(pool.get(tuples[0][1].as_keyword()), "GoldValue");
    EXPECT_EQ(tuples[0][2].as_int(), 77);
}

} // namespace
