#include <gtest/gtest.h>
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"

using namespace mora;

namespace {

Module parse_src(StringPool& pool, DiagBag& diags, const std::string& src) {
    Lexer lexer(src, "test.mora", pool, diags);
    Parser parser(lexer, pool, diags);
    return parser.parse_module();
}

} // namespace

TEST(CallExprTest, CallInHeadValue) {
    StringPool pool;
    DiagBag diags;
    // skyrim/set(W, :Damage, abs(N)) — abs(N) is head_args[2]
    auto mod = parse_src(pool, diags,
        "namespace t\n"
        "skyrim/set(W, :Damage, abs(N)):\n"
        "    form/weapon(W)\n");
    ASSERT_FALSE(diags.has_errors());

    ASSERT_EQ(mod.rules.size(), 1u);
    ASSERT_EQ(mod.rules[0].head_args.size(), 3u);
    const Expr& arg = mod.rules[0].head_args[2];
    const auto* call = std::get_if<CallExpr>(&arg.data);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(pool.get(call->name), "abs");
    EXPECT_EQ(call->args.size(), 1u);
}

TEST(CallExprTest, MaxWithBinaryArg) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_src(pool, diags,
        "namespace t\n"
        "skyrim/set(W, :Damage, max(0, N - 5)):\n"
        "    form/weapon(W)\n");
    ASSERT_FALSE(diags.has_errors());

    const Expr& arg = mod.rules[0].head_args[2];
    const auto* call = std::get_if<CallExpr>(&arg.data);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(pool.get(call->name), "max");
    ASSERT_EQ(call->args.size(), 2u);

    // First arg: int literal 0
    const auto* arg0 = std::get_if<IntLiteral>(&call->args[0].data);
    ASSERT_NE(arg0, nullptr);
    EXPECT_EQ(arg0->value, 0);

    // Second arg: BinaryExpr (Sub)
    const auto* arg1 = std::get_if<BinaryExpr>(&call->args[1].data);
    ASSERT_NE(arg1, nullptr);
    EXPECT_EQ(arg1->op, BinaryExpr::Op::Sub);
}

TEST(CallExprTest, CallInsideArithmetic) {
    StringPool pool;
    DiagBag diags;
    // 10 * VL + 5 * max(0, VL - PL)
    auto mod = parse_src(pool, diags,
        "namespace t\n"
        "skyrim/set(W, :Damage, 10 * VL + 5 * max(0, VL - PL)):\n"
        "    form/weapon(W)\n");
    ASSERT_FALSE(diags.has_errors());

    const Expr& arg = mod.rules[0].head_args[2];
    const auto* outer = std::get_if<BinaryExpr>(&arg.data);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->op, BinaryExpr::Op::Add);

    const auto* rhs = std::get_if<BinaryExpr>(&outer->right->data);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(rhs->op, BinaryExpr::Op::Mul);

    // rhs->right should be max(...)
    const auto* call = std::get_if<CallExpr>(&rhs->right->data);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(pool.get(call->name), "max");
    EXPECT_EQ(call->args.size(), 2u);
}
