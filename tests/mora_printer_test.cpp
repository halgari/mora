#include <gtest/gtest.h>
#include "mora/import/mora_printer.h"
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

class MoraPrinterTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(MoraPrinterTest, SimpleRule) {
    mora::Rule rule;
    rule.name = pool.intern("test_rule");

    // Head: test_rule(NPC)
    mora::Expr head;
    head.data = mora::VariableExpr{pool.intern("NPC"), {}, {}};
    rule.head_args.push_back(std::move(head));

    // Body: npc(NPC)
    mora::FactPattern fp;
    fp.name = pool.intern("npc");
    mora::Expr a1;
    a1.data = mora::VariableExpr{pool.intern("NPC"), {}, {}};
    fp.args.push_back(std::move(a1));
    mora::Clause c;
    c.data = std::move(fp);
    rule.body.push_back(std::move(c));

    // Effect: => add_keyword(NPC, :Tag)
    mora::Effect eff;
    eff.name = pool.intern("add_keyword");
    mora::Expr e1, e2;
    e1.data = mora::VariableExpr{pool.intern("NPC"), {}, {}};
    e2.data = mora::SymbolExpr{pool.intern("Tag"), {}, {}};
    eff.args.push_back(std::move(e1));
    eff.args.push_back(std::move(e2));
    rule.effects.push_back(std::move(eff));

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rule);
    EXPECT_NE(output.find("test_rule(NPC):"), std::string::npos);
    EXPECT_NE(output.find("    npc(NPC)"), std::string::npos);
    EXPECT_NE(output.find("    => add_keyword(NPC, :Tag)"), std::string::npos);
}

TEST_F(MoraPrinterTest, NegatedFact) {
    mora::Rule rule;
    rule.name = pool.intern("test");
    mora::Expr head;
    head.data = mora::VariableExpr{pool.intern("X"), {}, {}};
    rule.head_args.push_back(std::move(head));

    mora::FactPattern fp;
    fp.name = pool.intern("has_keyword");
    fp.negated = true;
    mora::Expr a1, a2;
    a1.data = mora::VariableExpr{pool.intern("X"), {}, {}};
    a2.data = mora::SymbolExpr{pool.intern("Foo"), {}, {}};
    fp.args.push_back(std::move(a1));
    fp.args.push_back(std::move(a2));
    mora::Clause c;
    c.data = std::move(fp);
    rule.body.push_back(std::move(c));

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rule);
    EXPECT_NE(output.find("not has_keyword(X, :Foo)"), std::string::npos);
}

TEST_F(MoraPrinterTest, Comment) {
    mora::MoraPrinter printer(pool);
    EXPECT_EQ(printer.print_comment("test comment"), "# test comment\n");
}

TEST_F(MoraPrinterTest, IntLiteral) {
    mora::Rule rule;
    rule.name = pool.intern("test");
    mora::Expr head;
    head.data = mora::VariableExpr{pool.intern("X"), {}, {}};
    rule.head_args.push_back(std::move(head));

    mora::FactPattern fp;
    fp.name = pool.intern("base_level");
    mora::Expr a1, a2;
    a1.data = mora::VariableExpr{pool.intern("X"), {}, {}};
    a2.data = mora::IntLiteral{25, {}};
    fp.args.push_back(std::move(a1));
    fp.args.push_back(std::move(a2));
    mora::Clause c;
    c.data = std::move(fp);
    rule.body.push_back(std::move(c));

    mora::MoraPrinter printer(pool);
    std::string output = printer.print_rule(rule);
    EXPECT_NE(output.find("base_level(X, 25)"), std::string::npos);
}
