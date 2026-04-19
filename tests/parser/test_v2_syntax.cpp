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

TEST(V2Syntax, MaintainRuleAnnotation) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "maintain r(F):\n"
        "    form/weapon(F)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].kind, RuleKind::Maintain);
}

TEST(V2Syntax, OnRuleAnnotation) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "on r(F):\n"
        "    form/weapon(F)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].kind, RuleKind::On);
}

TEST(V2Syntax, UnannotatedRuleIsStatic) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "r(F):\n"
        "    form/weapon(F)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(mod.rules[0].kind, RuleKind::Static);
}

TEST(V2Syntax, QualifiedHeadParsed) {
    StringPool pool;
    DiagBag diags;
    // Three qualified-head rules replace the old effect syntax.
    auto mod = parse_source(
        "namespace x.y\n"
        "skyrim/set(F, :Damage, 20):\n"
        "    form/weapon(F)\n"
        "skyrim/add(F, :Keyword, @Enchanted):\n"
        "    form/weapon(F)\n"
        "skyrim/remove(F, :Keyword, @Cursed):\n"
        "    form/weapon(F)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 3u);
    EXPECT_EQ(pool.get(mod.rules[0].qualifier), "skyrim");
    EXPECT_EQ(pool.get(mod.rules[0].name), "set");
    ASSERT_EQ(mod.rules[0].head_args.size(), 3u);
    EXPECT_EQ(pool.get(mod.rules[1].qualifier), "skyrim");
    EXPECT_EQ(pool.get(mod.rules[1].name), "add");
    EXPECT_EQ(pool.get(mod.rules[2].qualifier), "skyrim");
    EXPECT_EQ(pool.get(mod.rules[2].name), "remove");
    EXPECT_FALSE(diags.has_errors());
}

TEST(V2Syntax, UseDeclWithAsAndRefer) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "use form :as f\n"
        "use ref :refer [keyword, in_combat]\n"
        "use event :as e :refer [entered_location]\n",
        pool, diags);
    ASSERT_EQ(mod.use_decls.size(), 3u);

    EXPECT_EQ(pool.get(mod.use_decls[0].namespace_path), "form");
    EXPECT_EQ(pool.get(mod.use_decls[0].alias), "f");
    EXPECT_TRUE(mod.use_decls[0].refer.empty());

    EXPECT_EQ(pool.get(mod.use_decls[1].namespace_path), "ref");
    EXPECT_EQ(mod.use_decls[1].alias.index, 0u);
    ASSERT_EQ(mod.use_decls[1].refer.size(), 2u);
    EXPECT_EQ(pool.get(mod.use_decls[1].refer[0]), "keyword");
    EXPECT_EQ(pool.get(mod.use_decls[1].refer[1]), "in_combat");

    EXPECT_EQ(pool.get(mod.use_decls[2].alias), "e");
    ASSERT_EQ(mod.use_decls[2].refer.size(), 1u);
    EXPECT_EQ(pool.get(mod.use_decls[2].refer[0]), "entered_location");
}
