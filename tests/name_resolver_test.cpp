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
    EXPECT_EQ(fact->param_types.size(), 1u);
    EXPECT_EQ(fact->param_types[0].kind, mora::TypeKind::NpcID);
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
