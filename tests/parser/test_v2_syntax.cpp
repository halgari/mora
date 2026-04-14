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

TEST(V2Syntax, VerbEffectParsed) {
    StringPool pool;
    DiagBag diags;
    auto mod = parse_source(
        "namespace x.y\n"
        "r(F):\n"
        "    form/weapon(F)\n"
        "    => set form/damage(F, 20)\n"
        "    => add form/keyword(F, @Enchanted)\n"
        "    => remove form/keyword(F, @Cursed)\n",
        pool, diags);
    ASSERT_EQ(mod.rules.size(), 1u);
    const auto& r = mod.rules[0];
    ASSERT_EQ(r.effects.size(), 3u);
    EXPECT_EQ(r.effects[0].verb, VerbKind::Set);
    EXPECT_EQ(pool.get(r.effects[0].namespace_), "form");
    EXPECT_EQ(pool.get(r.effects[0].name), "damage");
    EXPECT_EQ(r.effects[1].verb, VerbKind::Add);
    EXPECT_EQ(r.effects[2].verb, VerbKind::Remove);
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
