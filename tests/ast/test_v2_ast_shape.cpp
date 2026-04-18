#include "mora/ast/ast.h"
#include <gtest/gtest.h>

using namespace mora;

TEST(V2Ast, RuleHasKindField) {
    Rule r{};
    EXPECT_EQ(r.kind, RuleKind::Static);
    r.kind = RuleKind::Maintain;
    EXPECT_EQ(r.kind, RuleKind::Maintain);
}

TEST(V2Ast, RuleHasQualifierField) {
    Rule r{};
    EXPECT_EQ(r.qualifier.index, 0u);  // empty by default
    EXPECT_EQ(r.name.index, 0u);
}

TEST(V2Ast, UseDeclHasAliasAndRefer) {
    UseDecl u{};
    EXPECT_EQ(u.alias.index, 0u);
    EXPECT_TRUE(u.refer.empty());
}

TEST(V2Ast, EditorIdExprVariantMember) {
    EditorIdExpr eid{};
    (void)eid;
    SUCCEED();
}
