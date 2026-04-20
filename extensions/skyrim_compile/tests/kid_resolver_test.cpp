#include <gtest/gtest.h>

#include "mora_skyrim_compile/kid_parser.h"
#include "mora_skyrim_compile/kid_resolver.h"
#include "mora_skyrim_compile/kid_rule_builder.h"

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/runtime_index.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <variant>

using namespace mora_skyrim_compile;

namespace {

KidLine mk_line(KidRef target, std::string_view item_type,
                std::vector<std::vector<KidRef>> filter_groups = {},
                std::vector<std::string> traits = {},
                double chance = 100.0) {
    KidLine l;
    l.target      = std::move(target);
    l.item_type   = std::string(item_type);
    l.chance      = chance;
    l.traits      = std::move(traits);
    l.source_line = 1;
    for (auto& g : filter_groups) {
        KidFilterGroup group;
        group.values = std::move(g);
        l.filter.push_back(std::move(group));
    }
    return l;
}

KidRef edid(std::string s) { KidRef r; r.editor_id = std::move(s); return r; }

KidRef wild(std::string pat) {
    KidRef r;
    r.editor_id = std::move(pat);
    r.wildcard  = true;
    return r;
}

KidRef fid_ref(uint32_t id, std::string mod) {
    KidRef r; r.formid = id; r.mod_file = std::move(mod); return r;
}

// ── Inspectors over synthesized Rule AST ──────────────────────────────

std::string head_target_editor_id(const mora::Rule& r,
                                    const mora::StringPool& pool) {
    if (r.head_args.size() < 3) return {};
    if (auto const* eid = std::get_if<mora::EditorIdExpr>(&r.head_args[2].data)) {
        return std::string(pool.get(eid->name));
    }
    return {};
}

bool head_is_skyrim_add(const mora::Rule& r, const mora::StringPool& pool) {
    return pool.get(r.qualifier) == "skyrim" && pool.get(r.name) == "add";
}

const mora::FactPattern* find_first_fact(const mora::Rule& r,
                                           std::string_view qualifier,
                                           std::string_view name,
                                           const mora::StringPool& pool) {
    for (const auto& clause : r.body) {
        if (auto const* fp = std::get_if<mora::FactPattern>(&clause.data)) {
            if (pool.get(fp->qualifier) == qualifier &&
                pool.get(fp->name) == name) {
                return fp;
            }
        }
    }
    return nullptr;
}

// Collect the EditorID arg of every form/keyword(X, @KW) clause in a rule.
std::vector<std::string> body_keyword_editor_ids(const mora::Rule& r,
                                                   const mora::StringPool& pool) {
    std::vector<std::string> out;
    for (const auto& clause : r.body) {
        auto const* fp = std::get_if<mora::FactPattern>(&clause.data);
        if (!fp) continue;
        if (pool.get(fp->qualifier) != "form" || pool.get(fp->name) != "keyword") continue;
        if (fp->args.size() < 2) continue;
        if (auto const* eid = std::get_if<mora::EditorIdExpr>(&fp->args[1].data)) {
            out.emplace_back(pool.get(eid->name));
        }
    }
    return out;
}

} // namespace

TEST(KidResolverTest, BasicLineProducesOneRule) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"TargetKW",  0x10000100u},
        {"FilterKW1", 0x10000200u},
    };

    KidFile f;
    f.path = "MyMod_KID.ini";
    f.lines.push_back(mk_line(edid("TargetKW"), "weapon",
                              {{edid("FilterKW1")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    EXPECT_EQ(diags.warning_count(), 0u);
    ASSERT_EQ(out.rules.size(), 1u);

    const mora::Rule& r = out.rules[0];
    EXPECT_TRUE(head_is_skyrim_add(r, pool));
    EXPECT_EQ(head_target_editor_id(r, pool), "TargetKW");
    EXPECT_NE(find_first_fact(r, "form", "weapon", pool), nullptr);

    auto kws = body_keyword_editor_ids(r, pool);
    ASSERT_EQ(kws.size(), 1u);
    EXPECT_EQ(kws[0], "FilterKW1");
}

TEST(KidResolverTest, UnresolvedTargetDropsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;  // empty

    KidFile f;
    f.lines.push_back(mk_line(edid("Missing"), "weapon"));

    auto out = resolve_kid_file(f, edids, pool, diags);
    EXPECT_TRUE(out.rules.empty());
    ASSERT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-unresolved");
}

TEST(KidResolverTest, FormidRefEmitsTaggedLiteralInHead) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;

    KidFile f;
    f.lines.push_back(mk_line(fid_ref(0x1A2B, "Mod.esp"), "weapon"));

    // KID resolver no longer consults plugin_runtime_index for FormID
    // refs — it emits a TaggedLiteralExpr("form", "0xNNN@Plugin.ext")
    // that the `#form` reader globalizes during expansion. Resolver
    // itself stays diagnostic-free for unresolved plugins.
    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    EXPECT_EQ(diags.warning_count(), 0u);

    const auto& head = out.rules[0].head_args;
    ASSERT_EQ(head.size(), 3u);
    const auto* tl = std::get_if<mora::TaggedLiteralExpr>(&head[2].data);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(pool.get(tl->tag), "form");
    EXPECT_EQ(pool.get(tl->payload), "0x1A2B@Mod.esp");
}

TEST(KidResolverTest, PartialFilterResolutionKeepsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x11u},
        {"A",      0x22u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{edid("A")}, {edid("B")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    // B's group fails entirely; A's group survives → 1 rule.
    ASSERT_EQ(out.rules.size(), 1u);
    auto kws = body_keyword_editor_ids(out.rules[0], pool);
    ASSERT_EQ(kws.size(), 1u);
    EXPECT_EQ(kws[0], "A");

    bool saw = false;
    for (auto& d : diags.all()) if (d.code == "kid-unresolved") saw = true;
    EXPECT_TRUE(saw);
}

TEST(KidResolverTest, AndOfOrsProducesOneRulePerGroup) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x11u}, {"A", 0x22u}, {"B", 0x33u}, {"C", 0x44u},
    };

    KidFile f;
    // Filter: (A AND B) OR C  → 2 rules, first has A and B conjuncts.
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{edid("A"), edid("B")}, {edid("C")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 2u);

    auto kws_a = body_keyword_editor_ids(out.rules[0], pool);
    ASSERT_EQ(kws_a.size(), 2u);
    std::sort(kws_a.begin(), kws_a.end());
    EXPECT_EQ(kws_a[0], "A");
    EXPECT_EQ(kws_a[1], "B");

    auto kws_c = body_keyword_editor_ids(out.rules[1], pool);
    ASSERT_EQ(kws_c.size(), 1u);
    EXPECT_EQ(kws_c[0], "C");
}

TEST(KidResolverTest, AllFilterValuesUnresolvedDropsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"Target", 0x11u}};

    KidFile f;
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{edid("Missing1")}, {edid("Missing2")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    EXPECT_TRUE(out.rules.empty());
    EXPECT_GE(diags.warning_count(), 1u);
}

TEST(KidResolverTest, CaseInsensitiveEditorIdResolution) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"MyKW", 0x55u}};

    KidFile f;
    f.lines.push_back(mk_line(edid("mykw"), "weapon"));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    // The CANONICAL editor_id (the one in the map) is what flows into
    // the AST — Skyrim treats EditorIDs case-insensitively, but the
    // evaluator's symbol lookup is case-sensitive on the StringId, so
    // the AST must reference the same casing the evaluator was loaded with.
    EXPECT_EQ(head_target_editor_id(out.rules[0], pool), "MyKW");
}

TEST(KidResolverTest, TraitEAddsEnchantedConjunct) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"T", 1u}};

    KidFile f;
    f.lines.push_back(mk_line(edid("T"), "weapon", {},
                              /*traits*/ {"E", "HEAVY"}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    const auto* ench = find_first_fact(out.rules[0], "form", "enchanted_with", pool);
    ASSERT_NE(ench, nullptr);
    EXPECT_FALSE(ench->negated);
}

TEST(KidResolverTest, UnknownTraitEmitsDiagnosticAndKeepsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"T", 1u}};

    KidFile f;
    // HEAVY / -HEAVY aren't in kid_trait_token_map yet. Unknown tokens
    // should surface a `kid-unknown-trait` warning but not drop the
    // line — the rest of it (target + item-type + filter) is still valid.
    f.lines.push_back(mk_line(edid("T"), "weapon", {},
                              /*traits*/ {"E", "HEAVY", "-HEAVY"}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);

    // Still only one enchanted_with conjunct — the HEAVY pair contributed nothing.
    size_t ench_conjuncts = 0;
    for (const auto& clause : out.rules[0].body) {
        if (auto const* fp = std::get_if<mora::FactPattern>(&clause.data)) {
            if (pool.get(fp->qualifier) == "form" &&
                pool.get(fp->name) == "enchanted_with") ench_conjuncts++;
        }
    }
    EXPECT_EQ(ench_conjuncts, 1u);

    size_t unknown_diag_count = 0;
    for (auto& d : diags.all()) {
        if (d.code == "kid-unknown-trait") unknown_diag_count++;
    }
    EXPECT_EQ(unknown_diag_count, 2u);
}

TEST(KidResolverTest, TraitNegEAddsNegatedEnchantedConjunct) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"T", 1u}};

    KidFile f;
    f.lines.push_back(mk_line(edid("T"), "armor", {}, {"-E"}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    const auto* ench = find_first_fact(out.rules[0], "form", "enchanted_with", pool);
    ASSERT_NE(ench, nullptr);
    EXPECT_TRUE(ench->negated);
}

TEST(KidResolverTest, FormidRefPayloadPreservesHexAndPluginCase) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;

    KidFile f;
    f.lines.push_back(mk_line(fid_ref(0x12AB, "MyMod.esp"), "weapon"));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);

    const auto& head = out.rules[0].head_args;
    ASSERT_EQ(head.size(), 3u);
    const auto* tl = std::get_if<mora::TaggedLiteralExpr>(&head[2].data);
    ASSERT_NE(tl, nullptr);
    // Payload carries hex (uppercase, no "0x" leading zeros) and plugin
    // filename as-written. The `#form` reader owns lowercasing /
    // globalization — the resolver just shuttles the text through.
    EXPECT_EQ(pool.get(tl->payload), "0x12AB@MyMod.esp");
}

TEST(KidResolverTest, FormidRefInFilterEmitsTaggedLiteralKeywordArg) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"Target", 1u}};

    KidFile f;
    // Filter contains a FormID ref — it should surface as
    // form/keyword(X, #form "0x800@MyMod.esp") in the synthesized rule
    // (the tagged literal sits in the 2nd arg of the keyword clause).
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{fid_ref(0x800, "MyMod.esp")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    const auto* kw = find_first_fact(out.rules[0], "form", "keyword", pool);
    ASSERT_NE(kw, nullptr);
    ASSERT_EQ(kw->args.size(), 2u);
    const auto* tl = std::get_if<mora::TaggedLiteralExpr>(&kw->args[1].data);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(pool.get(tl->tag), "form");
    EXPECT_EQ(pool.get(tl->payload), "0x800@MyMod.esp");
}

TEST(KidResolverTest, WildcardExpandsToOneRulePerMatch) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target",     0x10u},
        {"IronSword",  0x20u},
        {"IronAxe",    0x21u},
        {"SteelSword", 0x22u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{wild("Iron*")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    // 2 matches → 2 rules (each with one form/keyword conjunct).
    ASSERT_EQ(out.rules.size(), 2u);
    std::vector<std::string> matched;
    for (const auto& r : out.rules) {
        auto kws = body_keyword_editor_ids(r, pool);
        ASSERT_EQ(kws.size(), 1u);
        matched.push_back(kws[0]);
    }
    std::sort(matched.begin(), matched.end());
    EXPECT_EQ(matched[0], "IronAxe");
    EXPECT_EQ(matched[1], "IronSword");
}

TEST(KidResolverTest, WildcardNoMatchWarnsAndSkips) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x10u},
        {"Steel",  0x22u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{wild("Iron*")}, {edid("Steel")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    auto kws = body_keyword_editor_ids(out.rules[0], pool);
    ASSERT_EQ(kws.size(), 1u);
    EXPECT_EQ(kws[0], "Steel");
    bool saw_empty = false;
    for (auto& d : diags.all()) if (d.code == "kid-wildcard-empty") saw_empty = true;
    EXPECT_TRUE(saw_empty);
}

TEST(KidResolverTest, WildcardStarAloneRejected) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x10u}, {"A", 0x20u}, {"B", 0x21u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("Target"), "weapon", {{wild("*")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    EXPECT_TRUE(out.rules.empty());
    bool saw_all = false;
    for (auto& d : diags.all()) if (d.code == "kid-wildcard-all") saw_all = true;
    EXPECT_TRUE(saw_all);
}

// New in v2: wildcards inside an AND-group are now expanded and
// cross-producted with the group's other members. Each resulting tuple
// becomes its own OR-alternative (its own rule).
TEST(KidResolverTest, WildcardInsideAndGroupCrossProducts) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target",    0x10u},
        {"IronSword", 0x20u},
        {"IronAxe",   0x21u},
        {"Heavy",     0x30u},
    };

    KidFile f;
    // `Iron* + Heavy` — wildcard in an AND-group with a literal.
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{wild("Iron*"), edid("Heavy")}}));

    auto out = resolve_kid_file(f, edids, pool, diags);
    // Iron* matches IronSword and IronAxe → 2 cross-product tuples →
    // 2 rules, each with two form/keyword conjuncts (the matched item
    // AND Heavy).
    ASSERT_EQ(out.rules.size(), 2u);
    for (const auto& r : out.rules) {
        auto kws = body_keyword_editor_ids(r, pool);
        ASSERT_EQ(kws.size(), 2u);
        // One is "Heavy", the other is one of the iron variants.
        bool has_heavy = std::find(kws.begin(), kws.end(), "Heavy") != kws.end();
        EXPECT_TRUE(has_heavy);
    }
}

TEST(KidResolverTest, LowChanceDiagnosedButLineKept) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {{"T", 1u}};

    KidFile f;
    f.lines.push_back(mk_line(edid("T"), "weapon", {}, {}, /*chance*/ 50.0));

    auto out = resolve_kid_file(f, edids, pool, diags);
    ASSERT_EQ(out.rules.size(), 1u);
    EXPECT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-chance-ignored");
}
