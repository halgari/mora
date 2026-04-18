#include <gtest/gtest.h>
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
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

    // Count tuples in a skyrim/* relation where the first arg matches formid.
    size_t count_for_formid(mora::FactDB& db, const char* rel_name, uint32_t formid) {
        auto rel_id = pool.intern(rel_name);
        const auto& tuples = db.get_relation(rel_id);
        size_t count = 0;
        for (const auto& t : tuples) {
            if (!t.empty() && t[0].kind() == mora::Value::Kind::FormID &&
                t[0].as_formid() == formid) {
                ++count;
            }
        }
        return count;
    }

    // Find tuples in a relation for a given formid; returns value at col 2.
    std::vector<mora::Value> values_for_formid(mora::FactDB& db, const char* rel_name, uint32_t formid) {
        auto rel_id = pool.intern(rel_name);
        const auto& tuples = db.get_relation(rel_id);
        std::vector<mora::Value> result;
        for (const auto& t : tuples) {
            if (t.size() >= 3 && t[0].kind() == mora::Value::Kind::FormID &&
                t[0].as_formid() == formid) {
                result.push_back(t[2]);
            }
        }
        return result;
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

    evaluator.evaluate_module(mod, db);

    // add_keyword -> skyrim/add relation, field "Keywords", value 0xCCC
    auto vals = values_for_formid(db, "skyrim/add", 0x100);
    ASSERT_EQ(vals.size(), 1u);
    EXPECT_EQ(vals[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(vals[0].as_formid(), 0xCCCu);

    // NPC 0x200 has no matching faction, nothing emitted
    EXPECT_EQ(count_for_formid(db, "skyrim/add", 0x200), 0u);
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

    evaluator.evaluate_module(mod, db);

    auto vals = values_for_formid(db, "skyrim/add", 0x100);
    ASSERT_EQ(vals.size(), 1u);
    EXPECT_EQ(vals[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(vals[0].as_formid(), 0xDDDu);
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

    evaluator.evaluate_module(mod, db);

    // NPC 0x100 has level 25 >= 20 and is in BanditFaction -> SilverSword
    auto vals = values_for_formid(db, "skyrim/add", 0x100);
    ASSERT_EQ(vals.size(), 1u);
    EXPECT_EQ(vals[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(vals[0].as_formid(), 0xE01u);

    // NPC 0x200 is not in BanditFaction, nothing emitted
    EXPECT_EQ(count_for_formid(db, "skyrim/add", 0x200), 0u);
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

    evaluator.evaluate_module(mod, db);

    // Weapon 0x300 HAS keyword 0xBBB, so negation fails, no effect emitted
    EXPECT_EQ(count_for_formid(db, "skyrim/add", 0x300), 0u);
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

    evaluator.evaluate_module(mod, db);

    // 2 NPCs each get one tuple in skyrim/add
    auto rel_id = pool.intern("skyrim/add");
    const auto& tuples = db.get_relation(rel_id);
    EXPECT_EQ(tuples.size(), 2u);
}

// Test: KW in KWList where KWList is a list-typed Value from FactDB.
// Programmatically constructs the rule AST to avoid needing to register
// test_dist as a builtin in the parser's name resolver.
//
// Rule (conceptually):
//   test_rule(NPC):
//       test_dist(_, "keyword", KWList)
//       npc(NPC)
//       has_keyword(NPC, KW)
//       KW in KWList
//       => add_keyword(NPC, :Result)
TEST_F(EvaluatorTest, ElementInListVar) {
    // Build FactDB
    mora::FactDB db(pool);

    // test_dist(1, "keyword", [:kw_formid_A, :kw_formid_B])
    mora::Value kw_list = mora::Value::make_list({
        mora::Value::make_formid(0xA01),
        mora::Value::make_formid(0xA02),
    });
    db.add_fact(pool.intern("test_dist"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        kw_list,
    });

    // has_keyword(0x100, kw_formid_A)  — NPC 1 has keyword A
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0xA01),
    });
    // has_keyword(0x200, kw_formid_B)  — NPC 2 has keyword B
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x200),
        mora::Value::make_formid(0xA02),
    });

    // npc(0x100), npc(0x200)
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x200)});

    // Build Rule AST programmatically
    // Variables: NPC, KWList, KW
    mora::StringId v_npc    = pool.intern("NPC");
    mora::StringId v_kwlist = pool.intern("KWList");
    mora::StringId v_kw     = pool.intern("KW");

    auto var_expr = [](mora::StringId name) -> mora::Expr {
        return mora::Expr{mora::VariableExpr{name, {}}, {}};
    };
    auto str_expr = [&](const std::string& s) -> mora::Expr {
        return mora::Expr{mora::StringLiteral{pool.intern(s), {}}, {}};
    };

    mora::Rule rule;
    rule.name = pool.intern("test_rule");
    rule.head_args.push_back(var_expr(v_npc));

    // Clause 1: test_dist(_, "keyword", KWList)
    {
        mora::FactPattern fp;
        fp.name = pool.intern("test_dist");
        fp.args.push_back(mora::Expr{mora::DiscardExpr{{}}, {}});
        fp.args.push_back(str_expr("keyword"));
        fp.args.push_back(var_expr(v_kwlist));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }

    // Clause 2: npc(NPC)
    {
        mora::FactPattern fp;
        fp.name = pool.intern("npc");
        fp.args.push_back(var_expr(v_npc));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }

    // Clause 3: has_keyword(NPC, KW)
    {
        mora::FactPattern fp;
        fp.name = pool.intern("has_keyword");
        fp.args.push_back(var_expr(v_npc));
        fp.args.push_back(var_expr(v_kw));
        rule.body.push_back(mora::Clause{std::move(fp), {}});
    }

    // Clause 4: KW in KWList
    {
        mora::InClause ic;
        ic.variable = std::make_unique<mora::Expr>(var_expr(v_kw));
        ic.values.push_back(var_expr(v_kwlist));
        rule.body.push_back(mora::Clause{std::move(ic), {}});
    }

    // Effect: add_keyword(NPC, :Result)
    {
        mora::Effect eff;
        eff.name = pool.intern("add_keyword");
        eff.args.push_back(var_expr(v_npc));
        // :Result as a symbol expr — evaluator resolves it via set_symbol_formid
        eff.args.push_back(mora::Expr{mora::SymbolExpr{pool.intern("Result"), {}}, {}});
        rule.effects.push_back(std::move(eff));
    }

    mora::Module mod;
    mod.rules.push_back(std::move(rule));

    mora::Evaluator evaluator(pool, diags, db);
    evaluator.set_symbol_formid(pool.intern("Result"), 0xBEEF);

    evaluator.evaluate_module(mod, db);

    // Both NPCs should receive :Result in skyrim/add
    auto v1 = values_for_formid(db, "skyrim/add", 0x100);
    auto v2 = values_for_formid(db, "skyrim/add", 0x200);
    ASSERT_EQ(v1.size(), 1u);
    ASSERT_EQ(v2.size(), 1u);
    EXPECT_EQ(v1[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(v1[0].as_formid(), 0xBEEFu);
    EXPECT_EQ(v2[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(v2[0].as_formid(), 0xBEEFu);
}
