#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
// type_checker.h excluded in M2; deleted in M3
#include "mora/diag/renderer.h"

class IntegrationTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    bool check(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        // TypeChecker removed in M2; arity/type errors are runtime concerns
        return !diags.has_errors();
    }
};

TEST_F(IntegrationTest, CompleteValidFile) {
    EXPECT_TRUE(check(
        "namespace test.patches\n"
        "\n"
        "requires mod(\"Skyrim.esm\")\n"
        "\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n"
        "    form/faction(NPC, @BanditFaction)\n"
        "\n"
        "high_level(NPC):\n"
        "    form/base_level(NPC, Level)\n"
        "    Level >= 30\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    high_level(NPC)\n"
        "\n"
        "skyrim/add(Weapon, :Keyword, @VampireBane):\n"
        "    form/weapon(Weapon)\n"
        "    form/keyword(Weapon, @WeapMaterialSilver)\n"
        "    not form/keyword(Weapon, @WeapTypeGreatsword)\n"
    ));
}

TEST_F(IntegrationTest, ArityMismatchDetected) {
    EXPECT_FALSE(check("wrong(NPC):\n    form/npc(NPC, @Extra)\n"));
}

TEST_F(IntegrationTest, UnknownFactDetected) {
    EXPECT_FALSE(check(
        "wrong(NPC):\n"
        "    form/completely_fake(NPC)\n"
    ));
}

TEST_F(IntegrationTest, RuleCompositionWorks) {
    EXPECT_TRUE(check(
        "a(X):\n"
        "    form/npc(X)\n"
        "\n"
        "b(X):\n"
        "    a(X)\n"
        "\n"
        "skyrim/add(X, :Keyword, @TestKeyword):\n"
        "    b(X)\n"
    ));
}

TEST_F(IntegrationTest, DiagnosticsRenderForUnknownFact) {
    // With TypeChecker gone (M2), use a name-resolution error (unknown fact)
    // to verify the renderer produces non-empty error output.
    check("wrong(NPC):\n    totally_unknown_fact(NPC)\n");
    mora::DiagRenderer renderer(false);
    auto output = renderer.render_all(diags);
    // E011 = unknown fact or rule from NameResolver
    EXPECT_TRUE(output.find("E011") != std::string::npos) << output;
}
