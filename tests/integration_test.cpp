#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
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
        mora::TypeChecker checker(pool, diags, resolver);
        checker.check(mod);
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
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "high_level(NPC):\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 30\n"
        "\n"
        "elite_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    high_level(NPC)\n"
        "\n"
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    not has_keyword(Weapon, :WeapTypeGreatsword)\n"
        "    => add_keyword(Weapon, :VampireBane)\n"
        "\n"
        "bandit_weapons(NPC):\n"
        "    bandit(NPC)\n"
        "    level(NPC, Level)\n"
        "    Level >= 20 => add_item(NPC, :SilverSword)\n"
        "    Level < 20 => add_item(NPC, :IronSword)\n"
    ));
}

TEST_F(IntegrationTest, ArityMismatchDetected) {
    EXPECT_FALSE(check(
        "wrong(NPC):\n"
        "    npc(NPC, :Extra)\n"
    ));
}

TEST_F(IntegrationTest, UnknownFactDetected) {
    EXPECT_FALSE(check(
        "wrong(NPC):\n"
        "    completely_fake(NPC)\n"
    ));
}

TEST_F(IntegrationTest, RuleCompositionWorks) {
    EXPECT_TRUE(check(
        "a(X):\n"
        "    npc(X)\n"
        "\n"
        "b(X):\n"
        "    a(X)\n"
        "\n"
        "c(X):\n"
        "    b(X)\n"
        "    => add_keyword(X, :TestKeyword)\n"
    ));
}

TEST_F(IntegrationTest, DiagnosticsRender) {
    check(
        "wrong(NPC):\n"
        "    npc(NPC, :Extra)\n"
    );
    mora::DiagRenderer renderer(false);
    auto output = renderer.render_all(diags);
    EXPECT_NE(output.find("E020"), std::string::npos);
}
