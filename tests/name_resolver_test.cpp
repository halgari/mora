#include <gtest/gtest.h>
#include "mora/sema/name_resolver.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

class NameResolverTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(NameResolverTest, BuiltinFactsRegistered) {
    mora::NameResolver resolver(pool, diags);
    auto* fact = resolver.lookup_fact(pool.intern("npc"));
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->arity, 1u);
    EXPECT_TRUE(fact->is_builtin);
}

TEST_F(NameResolverTest, HasKeywordArity) {
    mora::NameResolver resolver(pool, diags);
    auto* fact = resolver.lookup_fact(pool.intern("has_keyword"));
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->arity, 2u);
}

TEST_F(NameResolverTest, LeveledEntryArity) {
    mora::NameResolver resolver(pool, diags);
    auto* fact = resolver.lookup_fact(pool.intern("leveled_entry"));
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->arity, 3u);
}

TEST_F(NameResolverTest, ResolveRuleReferencingBuiltin) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(NameResolverTest, DerivedRuleUsableAsFact) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(NameResolverTest, DerivedRuleArityRecorded) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_FALSE(diags.has_errors());
    // Derived rules get arity from their head arg count
    auto* fact = resolver.lookup_fact(pool.intern("bandit"));
    ASSERT_NE(fact, nullptr);
    EXPECT_EQ(fact->arity, 1u);
    EXPECT_FALSE(fact->is_builtin);
}

TEST_F(NameResolverTest, UnknownFactError) {
    auto mod = parse(
        "test(NPC):\n"
        "    nonexistent_fact(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_TRUE(diags.has_errors());
}

TEST_F(NameResolverTest, DuplicateRuleError) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "\n"
        "bandit(NPC):\n"
        "    npc(NPC)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    EXPECT_TRUE(diags.has_errors());
}
