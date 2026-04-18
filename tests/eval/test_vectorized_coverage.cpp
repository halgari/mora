// Plan 15 M1: verify every rule in example.mora vectorizes.
// After M1, vectorized_rules_count() must equal total rule count for every
// test fixture. This test asserts 5/5 for test_data/example.mora.

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace {

static std::filesystem::path locate_fixture(const std::string& filename) {
    std::filesystem::path cwd = std::filesystem::current_path();
    for (std::filesystem::path p = cwd; !p.empty(); p = p.parent_path()) {
        auto cand = p / "test_data" / filename;
        if (std::filesystem::exists(cand)) return cand;
        if (p == p.parent_path()) break;
    }
    return {};
}

static std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static mora::Module parse_and_resolve(mora::StringPool& pool,
                                       mora::DiagBag&    diags,
                                       const std::string& source,
                                       const std::string& filename) {
    mora::Lexer lexer(source, filename, pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    return mod;
}

// ── example.mora: 5 rules, all must vectorize after Plan 15 M1 ───────────────
//
// All 5 rules use @EditorID args (BanditFaction, ActorTypeNPC,
// WeapMaterialSilver, WeapTypeGreatsword, WeapMaterialIron). Before M1 they
// were blocked by is_simple_arg_expr not accepting EditorIdExpr. After M1 they
// must all take the vectorized path.

TEST(VectorizedCoverage, AllExampleMoraRulesVectorize) {
    auto path = locate_fixture("example.mora");
    ASSERT_FALSE(path.empty()) << "test_data/example.mora not found";

    std::string src = read_file(path);
    ASSERT_FALSE(src.empty());

    mora::StringPool pool;
    mora::DiagBag diags;
    auto mod = parse_and_resolve(pool, diags, src, path.string());
    for (auto const& d : diags.all())
        if (d.level == mora::DiagLevel::Error) ADD_FAILURE() << d.message;
    ASSERT_FALSE(diags.has_errors());

    // example.mora has 5 rules.
    ASSERT_EQ(mod.rules.size(), 5u);

    // Seed fake FormIDs for all @EditorID symbols used in example.mora.
    // The evaluator resolves these via set_symbol_formid; without them,
    // ScanOp would mark scans as no_match and produce zero rows (but
    // planning still succeeds — the rules vectorize).
    mora::FactDB db(pool);

    mora::Evaluator eval(pool, diags, db);
    eval.set_symbol_formid(pool.intern("BanditFaction"),      0x00028AD6);
    eval.set_symbol_formid(pool.intern("ActorTypeNPC"),        0x00013794);
    eval.set_symbol_formid(pool.intern("WeapMaterialSilver"),  0x000A61A9);
    eval.set_symbol_formid(pool.intern("WeapTypeGreatsword"),  0x0006D930);
    eval.set_symbol_formid(pool.intern("WeapMaterialIron"),    0x000A61A2);

    eval.evaluate_module(mod, db);

    // ALL 5 rules must have taken the vectorized path (Plan 15 M1 goal).
    EXPECT_EQ(eval.vectorized_rules_count(), 5u)
        << "Not all example.mora rules vectorized. "
        << eval.vectorized_rules_count() << "/5 vectorized.";
}

// ── Per-fixture vectorization sweep ─────────────────────────────────────────
//
// Walk every .mora file under test_data/ (except errors.mora which is a
// negative fixture). For each, parse+evaluate and assert 100% vectorization.
// Any fixture that fails the check is flagged — either close the gap or
// document why the rule shape is unsupported.
//
// Note: fixtures that use @EditorID without set_symbol_formid will still
// vectorize (planning succeeds), but produce zero effect rows (which is
// correct — the EditorID was unresolved). This tests the planning path,
// not the data path.

TEST(VectorizedCoverage, AllTestDataFixturesVectorize) {
    std::filesystem::path dir;
    {
        std::filesystem::path cwd = std::filesystem::current_path();
        for (std::filesystem::path p = cwd; !p.empty(); p = p.parent_path()) {
            auto candidate = p / "test_data";
            if (std::filesystem::exists(candidate) &&
                std::filesystem::is_directory(candidate)) {
                dir = candidate;
                break;
            }
            if (p == p.parent_path()) break;
        }
    }
    ASSERT_FALSE(dir.empty()) << "test_data/ not found";

    size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".mora") continue;
        const std::string stem = entry.path().stem().string();
        // Skip negative fixtures (expected to produce parse/resolve errors).
        if (stem == "errors") continue;

        SCOPED_TRACE(entry.path().string());

        std::string src = read_file(entry.path());
        mora::StringPool pool;
        mora::DiagBag diags;
        auto mod = parse_and_resolve(pool, diags, src, entry.path().string());

        // Skip fixtures with errors (they won't evaluate correctly).
        if (diags.has_errors()) continue;
        // Skip if no rules (nothing to vectorize).
        if (mod.rules.empty()) continue;

        mora::FactDB db(pool);
        mora::Evaluator eval(pool, diags, db);

        // Provide fake FormIDs for common @EditorIDs to ensure EditorIdExpr
        // args don't produce no_match in ScanOp, allowing full plan coverage.
        // The vectorized path is taken regardless; these just affect row count.
        eval.set_symbol_formid(pool.intern("BanditFaction"),     0x00028AD6);
        eval.set_symbol_formid(pool.intern("ActorTypeNPC"),       0x00013794);
        eval.set_symbol_formid(pool.intern("WeapMaterialSilver"), 0x000A61A9);
        eval.set_symbol_formid(pool.intern("WeapTypeGreatsword"), 0x0006D930);
        eval.set_symbol_formid(pool.intern("WeapMaterialIron"),   0x000A61A2);
        eval.set_symbol_formid(pool.intern("SomeFaction"),        0x00011111);
        eval.set_symbol_formid(pool.intern("RegionalGoodsTag"),   0x00022222);
        eval.set_symbol_formid(pool.intern("CWSonsFaction"),      0x00033333);

        eval.evaluate_module(mod, db);

        size_t const total = mod.rules.size();
        size_t const vect  = eval.vectorized_rules_count();

        // bandit_bounty.mora uses `player/gold` which maps to FieldId::Invalid
        // (not in the form model) → the planner cannot emit an EffectAppendOp
        // for it. This rule falls back intentionally in M1. Document:
        //   bandit_bounty.mora: 0/1 vectorized (unknown action add_gold)
        // All other fixtures must vectorize 100%.
        if (stem == "bandit_bounty") {
            // Intentional fallback: `add player/gold` is not in the field model.
            // vectorized_rules_count() == 0 is expected here in M1.
            // (M2 / Plan 16 will either add the relation or rewrite the rule.)
            EXPECT_EQ(vect, 0u)
                << stem << ": expected 0 vectorized (unknown action) but got " << vect;
        } else {
            EXPECT_EQ(vect, total)
                << stem << ": " << vect << "/" << total << " rules vectorized. "
                << "Some rule shapes may still fall back — fix or document.";
        }

        ++checked;
    }
    EXPECT_GT(checked, 0u) << "no .mora fixtures evaluated";
}

} // namespace
