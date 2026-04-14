#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/diagnostic.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace mora;

namespace {

std::filesystem::path locate_fixture() {
    std::filesystem::path cwd = std::filesystem::current_path();
    for (std::filesystem::path p = cwd; !p.empty(); p = p.parent_path()) {
        auto cand = p / "test_data" / "bandit_bounty.mora";
        if (std::filesystem::exists(cand)) return cand;
        if (p == p.parent_path()) break;
    }
    return {};
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST(BanditBounty, ParsesResolvesAndTypeChecks) {
    auto path = locate_fixture();
    ASSERT_FALSE(path.empty()) << "could not find test_data/bandit_bounty.mora";

    std::string src = read_file(path);
    ASSERT_FALSE(src.empty());

    StringPool pool;
    DiagBag diags;
    Lexer lex(src, path.string(), pool, diags);
    Parser parser(lex, pool, diags);
    Module mod = parser.parse_module();
    mod.source = src;

    NameResolver nr(pool, diags);
    nr.resolve(mod);

    TypeChecker tc(pool, diags, nr);
    tc.check(mod);

    for (const auto& d : diags.all()) {
        if (d.level == DiagLevel::Error) {
            ADD_FAILURE() << "[" << d.code << "] " << d.message
                          << " @ line " << d.span.start_line;
        }
    }
    EXPECT_EQ(diags.error_count(), 0u);

    ASSERT_EQ(mod.rules.size(), 1u);
    const Rule& rule = mod.rules[0];
    EXPECT_EQ(rule.kind, RuleKind::On);
    EXPECT_EQ(pool.get(rule.name), "bandit_bounty");
    EXPECT_EQ(rule.head_args.size(), 2u);

    // Expected: seven body clauses (killed + is_player + is_npc + base_form
    //           + faction + level(Victim) + level(Player))
    EXPECT_EQ(rule.body.size(), 7u);

    // Effect: add player/gold(Player, 10 * VL + 5 * max(0, VL - PL))
    ASSERT_EQ(rule.effects.size(), 1u);
    EXPECT_EQ(rule.effects[0].verb, VerbKind::Add);
    EXPECT_EQ(pool.get(rule.effects[0].namespace_), "player");
    EXPECT_EQ(pool.get(rule.effects[0].name), "gold");
    ASSERT_EQ(rule.effects[0].args.size(), 2u);

    // The value arg must be an Add(Mul(...), Mul(..., CallExpr(max, ...))).
    const Expr& value = rule.effects[0].args[1];
    const auto* outer = std::get_if<BinaryExpr>(&value.data);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Add);

    const auto* rhs = std::get_if<BinaryExpr>(&outer->right->data);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryExpr::Op::Mul);

    const auto* call = std::get_if<CallExpr>(&rhs->right->data);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(pool.get(call->name), "max");
    EXPECT_EQ(call->args.size(), 2u);
}
