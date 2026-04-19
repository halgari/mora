#include <gtest/gtest.h>
#include "mora_skyrim_compile/esp/override_filter.h"
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/plugin_index.h"
#include "mora_skyrim_compile/esp/record_types.h"

// ── OverrideFilter::observe / is_winner ──────────────────────────────

TEST(OverrideFilter, FirstObservationWins) {
    mora::OverrideFilter f;
    f.observe(0x00012345u, 0);
    EXPECT_TRUE(f.is_winner(0x00012345u, 0));
}

TEST(OverrideFilter, HigherLoadIndexBeatsLower) {
    // Two plugins declare the same global FormID. The later one
    // (higher load index) wins.
    mora::OverrideFilter f;
    f.observe(0x00012345u, 0);
    f.observe(0x00012345u, 2);
    EXPECT_FALSE(f.is_winner(0x00012345u, 0));
    EXPECT_TRUE (f.is_winner(0x00012345u, 2));
}

TEST(OverrideFilter, LowerLoadIndexDoesNotDisplaceWinner) {
    // observe() is called in load order, but just in case it isn't,
    // a later observe with a lower load_idx must not steal ownership.
    mora::OverrideFilter f;
    f.observe(0x00012345u, 5);
    f.observe(0x00012345u, 1);
    EXPECT_FALSE(f.is_winner(0x00012345u, 1));
    EXPECT_TRUE (f.is_winner(0x00012345u, 5));
}

TEST(OverrideFilter, UnobservedFormIdIsNotAWinner) {
    // Records whose global FormID was never observed (e.g., a
    // plugin that wasn't in the load order we built from) should
    // not be considered winners — conservative: emit nothing.
    mora::OverrideFilter f;
    f.observe(0x00012345u, 0);
    EXPECT_FALSE(f.is_winner(0xFEED1234u, 0));
}

TEST(OverrideFilter, EslSpaceFormIdsAreFirstClass) {
    // Light-master FormIDs look like 0xFEXXXYYY; the filter keys
    // on the full 32-bit global so they're no different from
    // regular FormIDs.
    mora::OverrideFilter f;
    f.observe(0xFE005123u, 4);
    f.observe(0xFE005123u, 10);
    EXPECT_TRUE (f.is_winner(0xFE005123u, 10));
    EXPECT_FALSE(f.is_winner(0xFE005123u, 4));
}

// ── OverrideFilter::build (end-to-end over PluginInfo) ──────────────

static mora::PluginInfo make_info_with_records(
    const std::string& filename,
    std::vector<std::string> masters,
    std::vector<uint32_t> local_form_ids,
    bool esl = false)
{
    mora::PluginInfo info;
    info.filename = filename;
    info.masters  = std::move(masters);
    info.flags    = esl ? mora::RecordFlags::ESL : 0;
    auto& slot = info.by_type["WEAP"];
    for (auto fid : local_form_ids) {
        slot.push_back(mora::RecordLocation{fid, 0, 0, 0});
    }
    return info;
}

TEST(OverrideFilterBuild, DawnguardOverrideOfSkyrimRecordLastWins) {
    // Dawnguard.esm declares a record with local FormID 0x00012345
    // (master=0 → Skyrim), globalizing to 0x00012345. Skyrim.esm
    // already declared 0x00012345 as a self-ref. Dawnguard is later
    // in load order, so Dawnguard's record is the winner.
    mora::RuntimeIndexMap rtmap;
    rtmap.index["skyrim.esm"]    = 0;
    rtmap.index["dawnguard.esm"] = 2;

    auto skyrim    = make_info_with_records("Skyrim.esm",    {},              {0x00012345u});
    auto dawnguard = make_info_with_records("Dawnguard.esm", {"Skyrim.esm"},  {0x00012345u});

    std::vector<mora::PluginInfo> plugins{skyrim, dawnguard};
    std::vector<uint32_t> load_idxs{0, 2};

    auto f = mora::OverrideFilter::build(plugins, rtmap, load_idxs);
    EXPECT_TRUE (f.is_winner(0x00012345u, /*load_idx=*/2));
    EXPECT_FALSE(f.is_winner(0x00012345u, /*load_idx=*/0));
}

TEST(OverrideFilterBuild, NewRecordInEslLandsInLightSpace) {
    // Light master at ESL index 5 declares a brand-new record with
    // object_id 0x123 (master byte = masters.size() = 1). Global
    // FormID is 0xFE005123. It should be the only winner for that
    // FormID.
    mora::RuntimeIndexMap rtmap;
    rtmap.index["skyrim.esm"] = 0;
    rtmap.index["light.esl"]  = 5;
    rtmap.light.insert("light.esl");

    auto skyrim = make_info_with_records("Skyrim.esm", {},              {0x00999999u});
    auto light  = make_info_with_records("Light.esl",  {"Skyrim.esm"},  {(1u << 24) | 0x123u}, /*esl=*/true);

    std::vector<mora::PluginInfo> plugins{skyrim, light};
    // Regular idx 0 for Skyrim; ESL load-position marker is encoded
    // through the rtmap — for the filter we pass the plugin's final
    // 8-bit or light-synthetic idx. Use distinct scalars so we can
    // assert ownership unambiguously.
    std::vector<uint32_t> load_idxs{0, 1};

    auto f = mora::OverrideFilter::build(plugins, rtmap, load_idxs);
    EXPECT_TRUE(f.is_winner(0xFE005123u, 1));
    EXPECT_TRUE(f.is_winner(0x00999999u, 0));
}

TEST(OverrideFilterBuild, RecordIntroducedByOverrideStillTracked) {
    // A record declared *only* in an override plugin (no earlier
    // source) still gets tracked — the override filter doesn't care
    // whether it's a brand-new record or an override of something.
    mora::RuntimeIndexMap rtmap;
    rtmap.index["skyrim.esm"] = 0;
    rtmap.index["mod.esp"]    = 5;

    auto skyrim = make_info_with_records("Skyrim.esm", {},             {0x00012345u});
    // Mod has no records referencing Skyrim — it declares its own.
    auto mod    = make_info_with_records("Mod.esp",    {"Skyrim.esm"}, {(1u << 24) | 0x000001u});

    std::vector<mora::PluginInfo> plugins{skyrim, mod};
    std::vector<uint32_t> load_idxs{0, 5};

    auto f = mora::OverrideFilter::build(plugins, rtmap, load_idxs);
    EXPECT_TRUE(f.is_winner(0x05000001u, 5));  // Mod's self-ref record
    EXPECT_TRUE(f.is_winner(0x00012345u, 0));  // Skyrim's untouched record
}
