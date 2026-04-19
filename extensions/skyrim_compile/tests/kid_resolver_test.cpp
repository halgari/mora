#include <gtest/gtest.h>
#include <algorithm>
#include "mora_skyrim_compile/kid_resolver.h"
#include "mora_skyrim_compile/kid_parser.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/runtime_index.h"

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

} // namespace

TEST(KidResolverTest, BasicLineEmitsDistAndFilter) {
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

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    EXPECT_EQ(diags.warning_count(), 0u);
    ASSERT_EQ(out.size(), 2u);

    EXPECT_EQ(out[0].relation, "ini/kid_dist");
    ASSERT_EQ(out[0].values.size(), 3u);
    EXPECT_EQ(out[0].values[0].as_int(),     1);
    EXPECT_EQ(out[0].values[1].as_formid(),  0x10000100u);
    EXPECT_EQ(pool.get(out[0].values[2].as_string()), "weapon");

    EXPECT_EQ(out[1].relation, "ini/kid_filter");
    ASSERT_EQ(out[1].values.size(), 4u);
    EXPECT_EQ(out[1].values[0].as_int(),    1);       // RuleID
    EXPECT_EQ(out[1].values[1].as_int(),    0);       // GroupID (first OR-group)
    EXPECT_EQ(pool.get(out[1].values[2].as_string()), "keyword");
    EXPECT_EQ(out[1].values[3].as_formid(), 0x10000200u);

    EXPECT_EQ(next, 2u);
}

TEST(KidResolverTest, UnresolvedTargetDropsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;  // empty

    KidFile f;
    f.path = "test_KID.ini";
    f.lines.push_back(mk_line(edid("Missing"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-unresolved");
    EXPECT_EQ(next, 1u);  // not incremented
}

TEST(KidResolverTest, FormidReferenceUnsupported) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;

    KidFile f;
    f.path = "test_KID.ini";
    f.lines.push_back(mk_line(fid_ref(0x1A2B, "Mod.esp"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    EXPECT_TRUE(out.empty());
    ASSERT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-formid-unsupported");
}

TEST(KidResolverTest, PartialFilterResolutionKeepsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x11u},
        {"A",      0x22u},
        // "B" missing on purpose
    };

    KidFile f;
    f.path = "t_KID.ini";
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{edid("A")}, {edid("B")}}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // dist + one filter row for A. B is dropped with warning. The
    // empty B group is omitted entirely, so A keeps GroupID=0 (its
    // original position) and the B group isn't emitted at all.
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[1].values[3].as_formid(), 0x22u);
    EXPECT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-unresolved");
}

TEST(KidResolverTest, AndOfOrsEmitsGroupIds) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x11u},
        {"A",      0x22u},
        {"B",      0x33u},
        {"C",      0x44u},
    };

    KidFile f;
    // Filter: (A AND B) OR C  ->  2 OR-groups, first has 2 AND-members.
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{edid("A"), edid("B")}, {edid("C")}}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // 1 kid_dist + 3 kid_filter rows (A, B in group 0; C in group 1).
    ASSERT_EQ(out.size(), 4u);

    EXPECT_EQ(out[1].relation, "ini/kid_filter");
    EXPECT_EQ(out[1].values[1].as_int(), 0);  // group 0
    EXPECT_EQ(out[1].values[3].as_formid(), 0x22u);  // A

    EXPECT_EQ(out[2].values[1].as_int(), 0);  // group 0 still
    EXPECT_EQ(out[2].values[3].as_formid(), 0x33u);  // B

    EXPECT_EQ(out[3].values[1].as_int(), 1);  // group 1
    EXPECT_EQ(out[3].values[3].as_formid(), 0x44u);  // C
}

TEST(KidResolverTest, AllFilterValuesUnresolvedDropsLine) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x11u},
    };

    KidFile f;
    f.path = "t_KID.ini";
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{edid("Missing1")}, {edid("Missing2")}}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    EXPECT_TRUE(out.empty());
    EXPECT_GE(diags.warning_count(), 1u);
}

TEST(KidResolverTest, CaseInsensitiveEditorId) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"MyKW", 0x55u},
    };

    KidFile f;
    f.path = "t_KID.ini";
    f.lines.push_back(mk_line(edid("mykw"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].values[1].as_formid(), 0x55u);
}

TEST(KidResolverTest, TraitsEmittedForEAndNegE) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"T", 1u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("T"), "weapon", {},
                              /*traits*/ {"E", "HEAVY", "-E"}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // dist + trait(E) + trait(-E). HEAVY is silently dropped (unsupported).
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[1].relation, "ini/kid_trait");
    EXPECT_EQ(pool.get(out[1].values[1].as_string()), "E");
    EXPECT_EQ(pool.get(out[2].values[1].as_string()), "-E");
}

TEST(KidResolverTest, FormidRefResolvesAgainstRegularPlugin) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    // MyMod.esp sits at runtime index 0x42 (regular, not ESL).
    std::unordered_map<std::string, uint32_t> plugins = {
        {"mymod.esp", 0x42u},
    };

    KidFile f;
    f.lines.push_back(mk_line(fid_ref(0x12AB, "MyMod.esp"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, &plugins, pool, diags, next);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].values[1].as_formid(), 0x420012ABu);
    EXPECT_EQ(diags.warning_count(), 0u);
}

TEST(KidResolverTest, FormidRefResolvesAgainstEslPlugin) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    // LightMod.esl at ESL slot 0x003. Descriptor has bit 31 set.
    std::unordered_map<std::string, uint32_t> plugins = {
        {"lightmod.esl", 0x003u | mora::ext::kRuntimeIdxEsl},
    };

    KidFile f;
    f.lines.push_back(mk_line(fid_ref(0x7F, "LightMod.esl"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, &plugins, pool, diags, next);
    ASSERT_EQ(out.size(), 1u);
    // Expected: 0xFE | slot<<12 | (local & 0xFFF) = 0xFE00307F
    EXPECT_EQ(out[0].values[1].as_formid(), 0xFE00307Fu);
}

TEST(KidResolverTest, FormidRefCaseInsensitivePluginLookup) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    std::unordered_map<std::string, uint32_t> plugins = {
        {"mymod.esp", 0x10u},
    };

    KidFile f;
    // Plugin filename capitalization drifts under Wine — resolver
    // lowercases before lookup.
    f.lines.push_back(mk_line(fid_ref(0xABC, "MYMOD.ESP"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, &plugins, pool, diags, next);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].values[1].as_formid(), 0x10000ABCu);
}

TEST(KidResolverTest, FormidRefMissingPluginDiagnosed) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids;
    std::unordered_map<std::string, uint32_t> plugins;  // empty

    KidFile f;
    f.lines.push_back(mk_line(fid_ref(0x01, "Unknown.esp"), "weapon"));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, &plugins, pool, diags, next);
    EXPECT_TRUE(out.empty());
    ASSERT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-missing-plugin");
}

TEST(KidResolverTest, WildcardExpandsAgainstEditorIds) {
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

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // 1 dist + 2 filter rows (IronSword, IronAxe); SteelSword doesn't match.
    ASSERT_EQ(out.size(), 3u);

    std::vector<uint32_t> matched;
    for (size_t i = 1; i < out.size(); ++i) {
        matched.push_back(out[i].values[3].as_formid());
    }
    std::sort(matched.begin(), matched.end());
    EXPECT_EQ(matched[0], 0x20u);
    EXPECT_EQ(matched[1], 0x21u);
}

TEST(KidResolverTest, WildcardNoMatchWarnsAndSkips) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x10u},
        {"Steel",  0x22u},
    };

    KidFile f;
    // Non-matching wildcard plus a literal. Line should keep the literal.
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{wild("Iron*")}, {edid("Steel")}}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // dist + 1 filter (Steel).
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[1].values[3].as_formid(), 0x22u);
    bool saw_empty = false;
    for (auto& d : diags.all()) if (d.code == "kid-wildcard-empty") saw_empty = true;
    EXPECT_TRUE(saw_empty);
}

TEST(KidResolverTest, WildcardStarAloneRejected) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target", 0x10u},
        {"A",      0x20u},
        {"B",      0x21u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{wild("*")}}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // No survivors → line dropped.
    EXPECT_TRUE(out.empty());
    bool saw_all = false;
    for (auto& d : diags.all()) if (d.code == "kid-wildcard-all") saw_all = true;
    EXPECT_TRUE(saw_all);
}

TEST(KidResolverTest, WildcardInsideAndGroupDropped) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"Target",    0x10u},
        {"IronSword", 0x20u},
        {"Heavy",     0x30u},
    };

    KidFile f;
    // `*Iron + Heavy` — wildcard inside an AND-group. Wildcard dropped
    // with a diagnostic; the group keeps only the literal Heavy.
    f.lines.push_back(mk_line(edid("Target"), "weapon",
                              {{wild("*Iron"), edid("Heavy")}}));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    // dist + 1 filter row for Heavy (group 0). Wildcard not expanded.
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[1].values[3].as_formid(), 0x30u);
    bool saw_in_and = false;
    for (auto& d : diags.all()) if (d.code == "kid-wildcard-in-and") saw_in_and = true;
    EXPECT_TRUE(saw_in_and);
}

TEST(KidResolverTest, LowChanceDiagnosedButLineKept) {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::unordered_map<std::string, uint32_t> edids = {
        {"T", 1u},
    };

    KidFile f;
    f.lines.push_back(mk_line(edid("T"), "weapon", {}, {}, /*chance*/ 50.0));

    uint32_t next = 1;
    auto out = resolve_kid_file(f, edids, nullptr, pool, diags, next);
    ASSERT_EQ(out.size(), 1u);  // dist only
    EXPECT_EQ(diags.warning_count(), 1u);
    EXPECT_EQ(diags.all()[0].code, "kid-chance-ignored");
}
