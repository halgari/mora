#include "mora_skyrim_compile/kid_rule_builder.h"

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>

using namespace mora_skyrim_compile;

namespace {

mora::SourceSpan span_at(uint32_t line) {
    mora::SourceSpan s{};
    s.file = "test.ini";
    s.start_line = line;
    s.end_line = line;
    return s;
}

ResolvedRef rr(uint32_t /*formid*/, std::string_view editor_id) {
    // formid is retained in the callsite for historical readability;
    // the builder only needs the editor_id name (the evaluator's symbol
    // table maps it to a FormID at scan time).
    return ResolvedRef::make_editor_id(std::string(editor_id));
}

ResolvedRef rr_tagged_form(std::string_view payload) {
    return ResolvedRef::make_tagged_form(std::string(payload));
}

TraitRef enchanted_trait(bool negated) {
    return TraitRef{TraitEvidence{"form", "enchanted_with", 1}, negated};
}

// Pull the StringId out of an Expr's variant for the kinds we care about.
struct ArgView {
    enum class Kind { Var, Keyword, EditorId, Other };
    Kind kind = Kind::Other;
    std::string text;
};

ArgView view_arg(const mora::Expr& e, const mora::StringPool& pool) {
    ArgView v;
    if (auto const* ve = std::get_if<mora::VariableExpr>(&e.data)) {
        v.kind = ArgView::Kind::Var;
        v.text = pool.get(ve->name);
    } else if (auto const* kl = std::get_if<mora::KeywordLiteral>(&e.data)) {
        v.kind = ArgView::Kind::Keyword;
        v.text = pool.get(kl->value);
    } else if (auto const* eid = std::get_if<mora::EditorIdExpr>(&e.data)) {
        v.kind = ArgView::Kind::EditorId;
        v.text = pool.get(eid->name);
    }
    return v;
}

const mora::FactPattern* fact_at(const mora::Rule& r, size_t idx) {
    return std::get_if<mora::FactPattern>(&r.body.at(idx).data);
}

} // namespace

TEST(KidRuleBuilderTest, NoFilterNoTraitProducesOneRule) {
    mora::StringPool pool;
    auto rules = build_rules(
        rr(0x100, "MyKW"),
        "weapon",
        /*filter_groups=*/{},
        /*traits=*/{},
        span_at(1), pool);

    ASSERT_EQ(rules.size(), 1u);
    const mora::Rule& r = rules[0];

    // Head: skyrim/add(X, :Keyword, @MyKW)
    EXPECT_EQ(pool.get(r.qualifier), "skyrim");
    EXPECT_EQ(pool.get(r.name), "add");
    ASSERT_EQ(r.head_args.size(), 3u);
    EXPECT_EQ(view_arg(r.head_args[0], pool).kind, ArgView::Kind::Var);
    EXPECT_EQ(view_arg(r.head_args[0], pool).text, "X");
    EXPECT_EQ(view_arg(r.head_args[1], pool).kind, ArgView::Kind::Keyword);
    EXPECT_EQ(view_arg(r.head_args[1], pool).text, ":Keyword");
    EXPECT_EQ(view_arg(r.head_args[2], pool).kind, ArgView::Kind::EditorId);
    EXPECT_EQ(view_arg(r.head_args[2], pool).text, "MyKW");

    // Body: just form/weapon(X)
    ASSERT_EQ(r.body.size(), 1u);
    const auto* fp = fact_at(r, 0);
    ASSERT_NE(fp, nullptr);
    EXPECT_EQ(pool.get(fp->qualifier), "form");
    EXPECT_EQ(pool.get(fp->name), "weapon");
    EXPECT_FALSE(fp->negated);
    ASSERT_EQ(fp->args.size(), 1u);
    EXPECT_EQ(view_arg(fp->args[0], pool).text, "X");
}

TEST(KidRuleBuilderTest, OneOrGroupOneAndMember) {
    mora::StringPool pool;
    auto rules = build_rules(
        rr(0x100, "TargetKW"),
        "weapon",
        {{rr(0x200, "FilterKW")}},
        {}, span_at(2), pool);

    ASSERT_EQ(rules.size(), 1u);
    const mora::Rule& r = rules[0];

    // Body: form/weapon(X), form/keyword(X, @FilterKW)
    ASSERT_EQ(r.body.size(), 2u);
    EXPECT_EQ(pool.get(fact_at(r, 0)->name), "weapon");

    const auto* kw = fact_at(r, 1);
    ASSERT_NE(kw, nullptr);
    EXPECT_EQ(pool.get(kw->qualifier), "form");
    EXPECT_EQ(pool.get(kw->name), "keyword");
    EXPECT_FALSE(kw->negated);
    ASSERT_EQ(kw->args.size(), 2u);
    EXPECT_EQ(view_arg(kw->args[0], pool).text, "X");
    EXPECT_EQ(view_arg(kw->args[1], pool).kind, ArgView::Kind::EditorId);
    EXPECT_EQ(view_arg(kw->args[1], pool).text, "FilterKW");
}

TEST(KidRuleBuilderTest, AndOfOrsExpandsToMultipleRules) {
    mora::StringPool pool;
    // Filter: (A AND B) OR C — two OR-groups, first has two AND-members.
    auto rules = build_rules(
        rr(0x100, "Target"),
        "armor",
        {{rr(0x200, "A"), rr(0x201, "B")}, {rr(0x202, "C")}},
        {}, span_at(3), pool);

    // Two rules — one per OR-group.
    ASSERT_EQ(rules.size(), 2u);

    // First rule: form/armor(X), form/keyword(X, @A), form/keyword(X, @B)
    ASSERT_EQ(rules[0].body.size(), 3u);
    EXPECT_EQ(pool.get(fact_at(rules[0], 0)->name), "armor");
    EXPECT_EQ(view_arg(fact_at(rules[0], 1)->args[1], pool).text, "A");
    EXPECT_EQ(view_arg(fact_at(rules[0], 2)->args[1], pool).text, "B");

    // Second rule: form/armor(X), form/keyword(X, @C)
    ASSERT_EQ(rules[1].body.size(), 2u);
    EXPECT_EQ(pool.get(fact_at(rules[1], 0)->name), "armor");
    EXPECT_EQ(view_arg(fact_at(rules[1], 1)->args[1], pool).text, "C");
}

TEST(KidRuleBuilderTest, TraitEAddsPositiveEnchantedWith) {
    mora::StringPool pool;
    auto rules = build_rules(
        rr(0x100, "T"), "weapon", /*filter_groups*/{},
        {enchanted_trait(/*negated*/ false)},
        span_at(4), pool);

    ASSERT_EQ(rules.size(), 1u);
    // Body: form/weapon(X), form/enchanted_with(X, _anonN)
    ASSERT_EQ(rules[0].body.size(), 2u);
    const auto* ench = fact_at(rules[0], 1);
    ASSERT_NE(ench, nullptr);
    EXPECT_EQ(pool.get(ench->qualifier), "form");
    EXPECT_EQ(pool.get(ench->name), "enchanted_with");
    EXPECT_FALSE(ench->negated);
    // Both args are variables (the second is a fresh anonymous slot).
    EXPECT_EQ(view_arg(ench->args[0], pool).kind, ArgView::Kind::Var);
    EXPECT_EQ(view_arg(ench->args[1], pool).kind, ArgView::Kind::Var);
    EXPECT_NE(view_arg(ench->args[1], pool).text, "X");  // distinct from X
}

TEST(KidRuleBuilderTest, TraitNegEAddsNegatedEnchantedWith) {
    mora::StringPool pool;
    auto rules = build_rules(
        rr(0x100, "T"), "armor", {},
        {enchanted_trait(/*negated*/ true)},
        span_at(5), pool);

    ASSERT_EQ(rules.size(), 1u);
    ASSERT_EQ(rules[0].body.size(), 2u);
    const auto* ench = fact_at(rules[0], 1);
    ASSERT_NE(ench, nullptr);
    EXPECT_EQ(pool.get(ench->name), "enchanted_with");
    EXPECT_TRUE(ench->negated);
}

TEST(KidRuleBuilderTest, FreshVariablesAreDistinctAcrossRules) {
    // Two OR-groups with traits: each rule should have its own fresh
    // variable name so the planner doesn't see false equalities.
    mora::StringPool pool;
    auto rules = build_rules(
        rr(0x100, "T"), "weapon",
        {{rr(0x200, "A")}, {rr(0x201, "B")}},
        {enchanted_trait(/*negated*/ false)},
        span_at(6), pool);

    ASSERT_EQ(rules.size(), 2u);
    // Rule 0 body: form/weapon(X), form/enchanted_with(X, _anon_0), form/keyword(X, @A)
    // Rule 1 body: form/weapon(X), form/enchanted_with(X, _anon_1), form/keyword(X, @B)
    auto fresh0 = view_arg(fact_at(rules[0], 1)->args[1], pool).text;
    auto fresh1 = view_arg(fact_at(rules[1], 1)->args[1], pool).text;
    EXPECT_NE(fresh0, fresh1);
}

TEST(KidRuleBuilderTest, EmptyTargetReturnsNoRules) {
    mora::StringPool pool;
    // Both editor_id AND tagged_payload are empty — a degenerate ref
    // the builder should reject.
    ResolvedRef bad;
    auto rules = build_rules(bad, "weapon", {}, {},
                              span_at(7), pool);
    EXPECT_TRUE(rules.empty());
}

TEST(KidRuleBuilderTest, TaggedFormTargetEmitsTaggedLiteralInHead) {
    mora::StringPool pool;
    auto target = rr_tagged_form("0x800123@MyMod.esp");
    auto rules = build_rules(target, "weapon", /*filter_groups*/{},
                              /*traits*/{}, span_at(8), pool);

    ASSERT_EQ(rules.size(), 1u);
    const mora::Rule& r = rules[0];
    ASSERT_EQ(r.head_args.size(), 3u);

    // Head arg 2 is a TaggedLiteralExpr carrying the form payload —
    // reader expansion replaces it with a FormIdLiteral at compile time.
    const auto* tl = std::get_if<mora::TaggedLiteralExpr>(&r.head_args[2].data);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(pool.get(tl->tag), "form");
    EXPECT_EQ(pool.get(tl->payload), "0x800123@MyMod.esp");
}
