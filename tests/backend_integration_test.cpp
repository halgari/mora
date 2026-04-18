#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/eval/phase_classifier.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"

class BackendIntegrationTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(BackendIntegrationTest, FullPipeline) {
    // Parse
    std::string source =
        "tag_all_npcs(NPC):\n"
        "    npc(NPC)\n"
        "    => add form/keyword(NPC, :Tagged)\n";

    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mod.source = source;
    ASSERT_FALSE(diags.has_errors());

    // Resolve + type check
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    ASSERT_FALSE(diags.has_errors());

    // Classify
    mora::PhaseClassifier classifier(pool);
    auto classifications = classifier.classify_module(mod);
    ASSERT_EQ(classifications.size(), 1u);
    EXPECT_EQ(classifications[0].phase, mora::Phase::Static);

    // Populate FactDB
    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x200)});

    // Evaluate
    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Tagged"), 0xFFF);
    evaluator.evaluate_module(mod, db);

    // add form/keyword -> skyrim/add, 2 NPCs each get one tuple
    auto rel_id = pool.intern("skyrim/add");
    const auto& tuples = db.get_relation(rel_id);
    EXPECT_EQ(tuples.size(), 2u);
}

TEST_F(BackendIntegrationTest, DynamicRulesSkipped) {
    std::string source =
        "static_rule(NPC):\n"
        "    npc(NPC)\n"
        "    => add form/keyword(NPC, :Static)\n"
        "\n"
        "dynamic_rule(NPC):\n"
        "    npc(NPC)\n"
        "    current_location(NPC, Loc)\n"
        "    => add form/keyword(NPC, :Dynamic)\n";

    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mod.source = source;

    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    mora::TypeChecker checker(pool, diags, resolver);
    checker.check(mod);
    ASSERT_FALSE(diags.has_errors());

    mora::PhaseClassifier classifier(pool);
    auto classifications = classifier.classify_module(mod);
    EXPECT_EQ(classifications[0].phase, mora::Phase::Static);
    EXPECT_EQ(classifications[1].phase, mora::Phase::Dynamic);

    mora::FactDB db(pool);
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});

    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Static"), 0xAA);
    evaluator.set_symbol_formid(pool.intern("Dynamic"), 0xBB);

    evaluator.evaluate_module(mod, db);

    // Only the static rule fires (dynamic rule has no current_location data).
    // skyrim/add should have exactly one tuple for formid 0x100 (the Static keyword).
    auto rel_id = pool.intern("skyrim/add");
    const auto& tuples = db.get_relation(rel_id);
    ASSERT_EQ(tuples.size(), 1u);
    ASSERT_GE(tuples[0].size(), 3u);
    EXPECT_EQ(tuples[0][0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(tuples[0][0].as_formid(), 0x100u);
    EXPECT_EQ(tuples[0][2].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(tuples[0][2].as_formid(), 0xAAu);
}
