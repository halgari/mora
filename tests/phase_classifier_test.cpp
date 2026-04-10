#include <gtest/gtest.h>
#include "mora/eval/phase_classifier.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"

class PhaseClassifierTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        return parser.parse_module();
    }
};

TEST_F(PhaseClassifierTest, StaticRule) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "    => add_keyword(NPC, :TestKeyword)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::PhaseClassifier classifier(pool);
    EXPECT_EQ(classifier.classify(mod.rules[0]), mora::Phase::Static);
}

TEST_F(PhaseClassifierTest, DynamicRuleFromInstanceFact) {
    auto mod = parse(
        "merchant_goods(NPC):\n"
        "    npc(NPC)\n"
        "    current_location(NPC, Loc)\n"
        "    => add_item(NPC, :TradeGoods)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::PhaseClassifier classifier(pool);
    EXPECT_EQ(classifier.classify(mod.rules[0]), mora::Phase::Dynamic);
}

TEST_F(PhaseClassifierTest, DerivedRuleNoEffectIsStatic) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::PhaseClassifier classifier(pool);
    EXPECT_EQ(classifier.classify(mod.rules[0]), mora::Phase::Static);
}

TEST_F(PhaseClassifierTest, ClassifyModule) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "    => add_keyword(NPC, :TestKeyword)\n"
        "\n"
        "dynamic_rule(NPC):\n"
        "    npc(NPC)\n"
        "    current_level(NPC, Level)\n"
        "    Level >= 30\n"
        "    => add_perk(NPC, :TestPerk)\n"
    );
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::PhaseClassifier classifier(pool);
    auto results = classifier.classify_module(mod);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].phase, mora::Phase::Static);
    EXPECT_EQ(results[1].phase, mora::Phase::Dynamic);
}
