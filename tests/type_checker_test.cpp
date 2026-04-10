#include <gtest/gtest.h>
#include "mora/sema/type_checker.h"
#include "mora/sema/name_resolver.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

class TypeCheckerTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse_source(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(TypeCheckerTest, ValidRule) {
    auto mod = parse_source(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(TypeCheckerTest, ArityMismatch) {
    auto mod = parse_source(
        "wrong(NPC):\n"
        "    npc(NPC, :Extra)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_TRUE(diags.has_errors());
}

TEST_F(TypeCheckerTest, VariableTypeInference) {
    auto mod = parse_source(
        "test(NPC):\n"
        "    npc(NPC)\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 20\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(TypeCheckerTest, UnusedVariableWarning) {
    auto mod = parse_source(
        "test(NPC):\n"
        "    npc(NPC)\n"
        "    base_level(NPC, Level)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_GT(diags.warning_count(), 0u);
}

TEST_F(TypeCheckerTest, DiscardNoWarning) {
    auto mod = parse_source(
        "test(NPC):\n"
        "    npc(NPC)\n"
        "    base_level(NPC, _)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_EQ(diags.warning_count(), 0u);
}

TEST_F(TypeCheckerTest, RuleComposition) {
    auto mod = parse_source(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 30\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    EXPECT_FALSE(diags.has_errors());
}
