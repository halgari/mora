#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include <gtest/gtest.h>

using namespace mora;

namespace {

Module parse_source(const std::string& src, StringPool& pool, DiagBag& diags) {
    Lexer lexer(src, "test.mora", pool, diags);
    Parser parser(lexer, pool, diags);
    return parser.parse_module();
}

} // namespace

TEST(V2Syntax, EditorIdParsesAsEditorIdExpr) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "r(F):\n"
        "    form/weapon(F)\n"
        "    form/keyword(F, @IronSword)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 1u);
    const Rule& r = mod.rules[0];
    ASSERT_GE(r.body.size(), 2u);
    const auto& fp = std::get<FactPattern>(r.body[1].data);
    ASSERT_EQ(fp.args.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<EditorIdExpr>(fp.args[1].data));
}

TEST(V2Syntax, NamespacedFactParses) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "r(F):\n"
        "    form/weapon(F)\n"
        "    form/keyword(F, @Iron)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 1u);
    ASSERT_GE(mod.rules[0].body.size(), 2u);
    const auto& fp0 = std::get<FactPattern>(mod.rules[0].body[0].data);
    EXPECT_EQ(pool.get(fp0.qualifier), "form");
    EXPECT_EQ(pool.get(fp0.name), "weapon");
    const auto& fp1 = std::get<FactPattern>(mod.rules[0].body[1].data);
    EXPECT_EQ(pool.get(fp1.qualifier), "form");
    EXPECT_EQ(pool.get(fp1.name), "keyword");
}
