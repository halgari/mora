#include <gtest/gtest.h>
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

class ParserTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(ParserTest, EmptyFile) {
    auto mod = parse("");
    EXPECT_TRUE(mod.rules.empty());
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, NamespaceDecl) {
    auto mod = parse("namespace my_mod.patches\n");
    ASSERT_TRUE(mod.ns.has_value());
    EXPECT_EQ(pool.get(mod.ns->name), "my_mod.patches");
}

TEST_F(ParserTest, RequiresDecl) {
    auto mod = parse("requires mod(\"Skyrim.esm\")\n");
    ASSERT_EQ(mod.requires_decls.size(), 1u);
    EXPECT_EQ(pool.get(mod.requires_decls[0].mod_name), "Skyrim.esm");
}

TEST_F(ParserTest, UseDecl) {
    auto mod = parse("use skyrim.record\n");
    ASSERT_EQ(mod.use_decls.size(), 1u);
    EXPECT_EQ(pool.get(mod.use_decls[0].namespace_path), "skyrim.record");
    EXPECT_TRUE(mod.use_decls[0].refer.empty());
}

TEST_F(ParserTest, UseDeclWithOnly) {
    auto mod = parse("use requiem.combat only [is_lethal, damage_mult]\n");
    ASSERT_EQ(mod.use_decls.size(), 1u);
    ASSERT_EQ(mod.use_decls[0].refer.size(), 2u);
    EXPECT_EQ(pool.get(mod.use_decls[0].refer[0]), "is_lethal");
    EXPECT_EQ(pool.get(mod.use_decls[0].refer[1]), "damage_mult");
}

TEST_F(ParserTest, SimpleRule) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(pool.get(mod.rules[0].name), "bandit");
    ASSERT_EQ(mod.rules[0].head_args.size(), 1u);
    EXPECT_EQ(mod.rules[0].body.size(), 2u);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, RuleWithEffect) {
    auto mod = parse(
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    => add_keyword(Weapon, :VampireBane)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].body.size(), 2u);
    EXPECT_EQ(mod.rules[0].effects.size(), 1u);
    EXPECT_EQ(pool.get(mod.rules[0].effects[0].name), "add_keyword");
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, RuleWithNegation) {
    auto mod = parse(
        "test(W):\n"
        "    weapon(W)\n"
        "    not has_keyword(W, :Foo)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    ASSERT_EQ(mod.rules[0].body.size(), 2u);
    auto& clause = mod.rules[0].body[1];
    auto* fact = std::get_if<mora::FactPattern>(&clause.data);
    ASSERT_NE(fact, nullptr);
    EXPECT_TRUE(fact->negated);
}

TEST_F(ParserTest, RuleWithConditionalEffects) {
    auto mod = parse(
        "bandit_weapons(NPC):\n"
        "    npc(NPC)\n"
        "    level(NPC, Level)\n"
        "    Level >= 20 => add_item(NPC, :SilverSword)\n"
        "    Level < 20 => add_item(NPC, :IronSword)\n"
    );
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].body.size(), 2u);
    EXPECT_EQ(mod.rules[0].conditional_effects.size(), 2u);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, MultipleRules) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "high_level(NPC):\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 30\n"
    );
    ASSERT_EQ(mod.rules.size(), 2u);
    EXPECT_EQ(pool.get(mod.rules[0].name), "bandit");
    EXPECT_EQ(pool.get(mod.rules[1].name), "high_level");
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, FullFile) {
    auto mod = parse(
        "namespace my_mod.patches\n"
        "\n"
        "requires mod(\"Skyrim.esm\")\n"
        "\n"
        "use skyrim.record\n"
        "\n"
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "vampire_bane(Weapon):\n"
        "    weapon(Weapon)\n"
        "    has_keyword(Weapon, :WeapMaterialSilver)\n"
        "    not has_keyword(Weapon, :WeapTypeGreatsword)\n"
        "    => add_keyword(Weapon, :VampireBane)\n"
    );
    ASSERT_TRUE(mod.ns.has_value());
    EXPECT_EQ(mod.requires_decls.size(), 1u);
    EXPECT_EQ(mod.use_decls.size(), 1u);
    EXPECT_EQ(mod.rules.size(), 2u);
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(ParserTest, ErrorRecovery) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC\n"           // missing closing paren
        "\n"
        "other(W):\n"
        "    weapon(W)\n"
    );
    EXPECT_TRUE(diags.has_errors());
    EXPECT_GE(mod.rules.size(), 1u);
}
