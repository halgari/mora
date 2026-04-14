#include <gtest/gtest.h>
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

using namespace mora;

namespace {

// Helper: parse a source string and return the module.
Module parse_src(StringPool& pool, DiagBag& diags, const std::string& src) {
    Lexer lexer(src, "test.mora", pool, diags);
    Parser parser(lexer, pool, diags);
    return parser.parse_module();
}

// Helper: extract the single effect value-arg expression from a module.
// The rule body is expected to be:
//   namespace t
//   r(X):
//       => set form/damage(X, <expr>)
const Expr& effect_value_expr(const Module& mod) {
    EXPECT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].effects.size(), 1u);
    EXPECT_GE(mod.rules[0].effects[0].args.size(), 2u);
    return mod.rules[0].effects[0].args[1];
}

} // namespace

TEST(PrecedenceTest, AdditiveLower_Than_Multiplicative_Right) {
    StringPool pool;
    DiagBag diags;
    // 1 + 2 * 3 should parse as Add(1, Mul(2, 3))
    auto mod = parse_src(pool, diags,
        "namespace t\n"
        "r(X):\n"
        "    => set form/damage(X, 1 + 2 * 3)\n");
    ASSERT_FALSE(diags.has_errors());

    const Expr& e = effect_value_expr(mod);
    const auto* outer = std::get_if<BinaryExpr>(&e.data);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Add);

    const auto* lhs = std::get_if<IntLiteral>(&outer->left->data);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->value, 1);

    const auto* rhs = std::get_if<BinaryExpr>(&outer->right->data);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryExpr::Op::Mul);

    const auto* rhs_l = std::get_if<IntLiteral>(&rhs->left->data);
    const auto* rhs_r = std::get_if<IntLiteral>(&rhs->right->data);
    ASSERT_NE(rhs_l, nullptr);
    ASSERT_NE(rhs_r, nullptr);
    EXPECT_EQ(rhs_l->value, 2);
    EXPECT_EQ(rhs_r->value, 3);
}

TEST(PrecedenceTest, AdditiveLower_Than_Multiplicative_Left) {
    StringPool pool;
    DiagBag diags;
    // 1 * 2 + 3 should parse as Add(Mul(1, 2), 3)
    auto mod = parse_src(pool, diags,
        "namespace t\n"
        "r(X):\n"
        "    => set form/damage(X, 1 * 2 + 3)\n");
    ASSERT_FALSE(diags.has_errors());

    const Expr& e = effect_value_expr(mod);
    const auto* outer = std::get_if<BinaryExpr>(&e.data);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Add);

    const auto* lhs = std::get_if<BinaryExpr>(&outer->left->data);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->op, BinaryExpr::Op::Mul);

    const auto* lhs_l = std::get_if<IntLiteral>(&lhs->left->data);
    const auto* lhs_r = std::get_if<IntLiteral>(&lhs->right->data);
    ASSERT_NE(lhs_l, nullptr);
    ASSERT_NE(lhs_r, nullptr);
    EXPECT_EQ(lhs_l->value, 1);
    EXPECT_EQ(lhs_r->value, 2);

    const auto* rhs = std::get_if<IntLiteral>(&outer->right->data);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->value, 3);
}

TEST(PrecedenceTest, ComplexBountyExpression) {
    StringPool pool;
    DiagBag diags;
    // 10 * VL + 5 * (VL - PL) — outer Add, both sides Mul
    auto mod = parse_src(pool, diags,
        "namespace t\n"
        "r(X, VL, PL):\n"
        "    => set form/damage(X, 10 * VL + 5 * (VL - PL))\n");
    ASSERT_FALSE(diags.has_errors());

    const Expr& e = effect_value_expr(mod);
    const auto* outer = std::get_if<BinaryExpr>(&e.data);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Add);

    const auto* lhs = std::get_if<BinaryExpr>(&outer->left->data);
    ASSERT_NE(lhs, nullptr);
    EXPECT_EQ(lhs->op, BinaryExpr::Op::Mul);

    const auto* rhs = std::get_if<BinaryExpr>(&outer->right->data);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryExpr::Op::Mul);
}
