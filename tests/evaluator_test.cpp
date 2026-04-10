#include <gtest/gtest.h>
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"

class EvaluatorTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    mora::Module parse(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mod.source = source;
        mora::NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        return mod;
    }

    mora::FactDB make_test_db() {
        mora::FactDB db(pool);
        db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
        db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x200)});
        db.add_fact(pool.intern("has_faction"), {mora::Value::make_formid(0x100), mora::Value::make_formid(0xAAA)});
        db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x100), mora::Value::make_int(25)});
        db.add_fact(pool.intern("base_level"), {mora::Value::make_formid(0x200), mora::Value::make_int(10)});
        db.add_fact(pool.intern("weapon"), {mora::Value::make_formid(0x300)});
        db.add_fact(pool.intern("has_keyword"), {mora::Value::make_formid(0x300), mora::Value::make_formid(0xBBB)});
        return db;
    }
};

TEST_F(EvaluatorTest, SimpleEffectRule) {
    auto mod = parse(
        "add_kw(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :TestFaction)\n"
        "    => add_keyword(NPC, :TestKeyword)\n"
    );
    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("TestFaction"), 0xAAA);
    evaluator.set_symbol_formid(pool.intern("TestKeyword"), 0xCCC);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].field, mora::FieldId::Keywords);
    EXPECT_EQ(patches[0].op, mora::FieldOp::Add);
    EXPECT_EQ(patches[0].value.as_formid(), 0xCCCu);
    EXPECT_TRUE(resolved.get_patches_for(0x200).empty());
}

TEST_F(EvaluatorTest, DerivedRuleComposition) {
    auto mod = parse(
        "bandit(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "\n"
        "tag_bandit(NPC):\n"
        "    bandit(NPC)\n"
        "    => add_keyword(NPC, :IsBandit)\n"
    );
    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("BanditFaction"), 0xAAA);
    evaluator.set_symbol_formid(pool.intern("IsBandit"), 0xDDD);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_formid(), 0xDDDu);
}

TEST_F(EvaluatorTest, ConditionalEffect) {
    auto mod = parse(
        "bandit_gear(NPC):\n"
        "    npc(NPC)\n"
        "    has_faction(NPC, :BanditFaction)\n"
        "    base_level(NPC, Level)\n"
        "    Level >= 20 => add_item(NPC, :SilverSword)\n"
        "    Level < 20 => add_item(NPC, :IronSword)\n"
    );
    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("BanditFaction"), 0xAAA);
    evaluator.set_symbol_formid(pool.intern("SilverSword"), 0xE01);
    evaluator.set_symbol_formid(pool.intern("IronSword"), 0xE02);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_formid(), 0xE01u);
    EXPECT_TRUE(resolved.get_patches_for(0x200).empty());
}

TEST_F(EvaluatorTest, NegationInBody) {
    auto mod = parse(
        "non_silver(W):\n"
        "    weapon(W)\n"
        "    not has_keyword(W, :Silver)\n"
        "    => add_keyword(W, :NonSilver)\n"
    );
    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Silver"), 0xBBB);
    evaluator.set_symbol_formid(pool.intern("NonSilver"), 0xFFF);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();
    // Weapon 0x300 HAS keyword 0xBBB, so negation fails, no patch
    EXPECT_TRUE(resolved.get_patches_for(0x300).empty());
}

TEST_F(EvaluatorTest, RuleMatchCount) {
    auto mod = parse(
        "tag_all(NPC):\n"
        "    npc(NPC)\n"
        "    => add_keyword(NPC, :Tagged)\n"
    );
    auto db = make_test_db();
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Tagged"), 0xFFF);

    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();
    EXPECT_EQ(resolved.patch_count(), 2u); // 2 NPCs
}
