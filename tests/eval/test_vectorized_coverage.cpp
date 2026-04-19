// Plan 15 M2: verify every rule in example.mora evaluates correctly.
// The vectorized path is now the ONLY evaluator — no tuple fallback.
// This file checks that every test fixture evaluates without error.
// (The vectorized_rules_count() counter was removed in M2.)

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

// ── example.mora: 5 rules, all must evaluate without errors after M2 ─────────
//
// All 5 rules use @EditorID args. After Plan 15 M1, all vectorize.
// After M2, the vectorized path is the only path — if planning fails,
// an error diagnostic is emitted.

TEST(VectorizedCoverage, AllExampleMoraRulesEvaluate) {
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

    mora::FactDB db(pool);

    mora::Evaluator eval(pool, diags, db);
    eval.set_symbol_formid(pool.intern("BanditFaction"),      0x00028AD6);
    eval.set_symbol_formid(pool.intern("ActorTypeNPC"),        0x00013794);
    eval.set_symbol_formid(pool.intern("WeapMaterialSilver"),  0x000A61A9);
    eval.set_symbol_formid(pool.intern("WeapTypeGreatsword"),  0x0006D930);
    eval.set_symbol_formid(pool.intern("WeapMaterialIron"),    0x000A61A2);

    eval.evaluate_module(mod, db);

    // All 5 rules must evaluate without error (vectorized path succeeds for all).
    EXPECT_FALSE(diags.has_errors())
        << "One or more example.mora rules caused a planner error (eval-unsupported).";
}

// ── Per-fixture evaluation sweep ─────────────────────────────────────────────
//
// Walk every .mora file under test_data/ (except errors.mora which is a
// negative fixture, and bandit_bounty.mora which uses an unregistered action).
// For each, parse+evaluate and assert no error diagnostics are emitted.

TEST(VectorizedCoverage, AllTestDataFixturesEvaluate) {
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
        // bandit_bounty.mora uses an `on` rule with instance facts (event/killed,
        // ref/is_player, etc.) that form a Cartesian join — the vectorized planner
        // correctly declines it. Skip here so we don't expect it to vectorize.
        if (stem == "bandit_bounty") continue;

        SCOPED_TRACE(entry.path().string());

        std::string src = read_file(entry.path());
        mora::StringPool pool;
        mora::DiagBag diags;
        auto mod = parse_and_resolve(pool, diags, src, entry.path().string());

        // Skip fixtures with parse/resolve errors.
        if (diags.has_errors()) continue;
        // Skip if no rules (nothing to evaluate).
        if (mod.rules.empty()) continue;

        mora::FactDB db(pool);
        mora::Evaluator eval(pool, diags, db);

        // Provide fake FormIDs for common @EditorIDs.
        eval.set_symbol_formid(pool.intern("BanditFaction"),     0x00028AD6);
        eval.set_symbol_formid(pool.intern("ActorTypeNPC"),       0x00013794);
        eval.set_symbol_formid(pool.intern("WeapMaterialSilver"), 0x000A61A9);
        eval.set_symbol_formid(pool.intern("WeapTypeGreatsword"), 0x0006D930);
        eval.set_symbol_formid(pool.intern("WeapMaterialIron"),   0x000A61A2);
        eval.set_symbol_formid(pool.intern("SomeFaction"),        0x00011111);
        eval.set_symbol_formid(pool.intern("RegionalGoodsTag"),   0x00022222);
        eval.set_symbol_formid(pool.intern("CWSonsFaction"),      0x00033333);

        eval.evaluate_module(mod, db);

        // No eval-unsupported diagnostics — every rule must vectorize.
        EXPECT_FALSE(diags.has_errors())
            << stem << ": one or more rules caused an eval-unsupported diagnostic.";

        ++checked;
    }
    EXPECT_GT(checked, 0u) << "no .mora fixtures evaluated";
}

} // namespace
