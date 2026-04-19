#include <gtest/gtest.h>
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/plugin_index.h"
#include "mora_skyrim_compile/esp/record_types.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

// ── LoadOrder::from_plugins_txt ──────────────────────────────────────

class PluginsTxtTest : public ::testing::Test {
protected:
    fs::path tmp_dir;

    void SetUp() override {
        tmp_dir = fs::temp_directory_path() /
                  ("mora_plugins_txt_" + std::to_string(::getpid()));
        fs::create_directories(tmp_dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmp_dir, ec);
    }

    fs::path write(const std::string& name, const std::string& body) {
        auto p = tmp_dir / name;
        std::ofstream f(p);
        f << body;
        return p;
    }

    // Seed an empty stub file in tmp_dir so `from_plugins_txt`'s
    // on-disk lookup finds it. Content doesn't matter for the parser
    // tests — `resolve_entries` isn't called here.
    void touch(const std::string& name) {
        std::ofstream f(tmp_dir / name, std::ios::binary);
        (void)f;
    }
};

TEST_F(PluginsTxtTest, ReadsEnabledEntriesInOrder) {
    touch("Skyrim.esm");
    touch("Update.esm");
    touch("Dawnguard.esm");
    touch("ModEnabled.esp");
    auto txt = write("Plugins.txt",
        "# header comment\n"
        "*Skyrim.esm\n"
        "*Update.esm\n"
        "*Dawnguard.esm\n"
        "ModDisabled.esp\n"       // missing *, disabled
        "*ModEnabled.esp\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);

    ASSERT_EQ(lo.plugins.size(), 4u);
    EXPECT_EQ(lo.plugins[0].filename(), "Skyrim.esm");
    EXPECT_EQ(lo.plugins[1].filename(), "Update.esm");
    EXPECT_EQ(lo.plugins[2].filename(), "Dawnguard.esm");
    EXPECT_EQ(lo.plugins[3].filename(), "ModEnabled.esp");
    EXPECT_TRUE(lo.missing.empty());
}

TEST_F(PluginsTxtTest, ForcesSkyrimEsmFirst) {
    // If Plugins.txt lists Update.esm before Skyrim.esm (or omits
    // Skyrim.esm entirely), the engine still loads Skyrim.esm first.
    // Our parser must reflect that invariant — but only if Skyrim.esm
    // actually exists on disk; we don't fabricate a phantom path.
    touch("Skyrim.esm");
    touch("Update.esm");
    touch("Modish.esp");
    auto txt = write("Plugins.txt",
        "*Update.esm\n"
        "*Modish.esp\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);

    ASSERT_GE(lo.plugins.size(), 1u);
    EXPECT_EQ(lo.plugins.front().filename(), "Skyrim.esm");
    EXPECT_TRUE(lo.missing.empty());
}

TEST_F(PluginsTxtTest, MissingSkyrimEsmRecordedNotFabricated) {
    // If Skyrim.esm is genuinely missing from Data/, the parser must
    // flag it in `missing` rather than silently inserting a nonexistent
    // path that'd crash downstream MmapFile parsing.
    touch("SomethingElse.esp");
    auto txt = write("Plugins.txt", "*SomethingElse.esp\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);
    // Skyrim.esm should NOT be prepended since no file backs it.
    for (auto& p : lo.plugins) {
        EXPECT_NE(p.filename(), "Skyrim.esm");
    }
    ASSERT_EQ(lo.missing.size(), 1u);
    EXPECT_EQ(lo.missing[0], "Skyrim.esm");
}

TEST_F(PluginsTxtTest, TrimsCarriageReturns) {
    touch("Skyrim.esm");
    touch("Mod.esp");
    auto txt = write("Plugins.txt",
        "*Skyrim.esm\r\n"
        "*Mod.esp\r\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);
    ASSERT_EQ(lo.plugins.size(), 2u);
    EXPECT_EQ(lo.plugins[0].filename(), "Skyrim.esm");
    EXPECT_EQ(lo.plugins[1].filename(), "Mod.esp");
}

TEST_F(PluginsTxtTest, CaseInsensitiveMatchUsesOnDiskCasing) {
    // Runtime (Wine) treats filenames as case-insensitive, so Skyrim.ccc
    // and on-disk casing routinely drift. Compile-time must resolve
    // through the real file instead of silently dropping the plugin,
    // which would shift every downstream ESL light index.
    touch("Skyrim.esm");
    touch("ccMTYSSE001-KnightsOfTheNine.esl");   // real on-disk casing
    auto txt = write("Plugins.txt",
        "*Skyrim.esm\n"
        "*ccMTYSSE001-KnightsoftheNine.esl\n");  // manifest casing

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);
    ASSERT_EQ(lo.plugins.size(), 2u);
    EXPECT_EQ(lo.plugins[1].filename(), "ccMTYSSE001-KnightsOfTheNine.esl");
    EXPECT_TRUE(lo.missing.empty());
}

TEST_F(PluginsTxtTest, ReportsTrulyMissingPlugins) {
    // A plugin listed in plugins.txt that genuinely isn't on disk —
    // typo, deleted mod, case-insensitive match also fails — gets
    // recorded in `missing` so main.cpp can emit a warning instead of
    // silently dropping the entry.
    touch("Skyrim.esm");
    auto txt = write("Plugins.txt",
        "*Skyrim.esm\n"
        "*NotAPlugin.esp\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);
    EXPECT_EQ(lo.plugins.size(), 1u);
    ASSERT_EQ(lo.missing.size(), 1u);
    EXPECT_EQ(lo.missing[0], "NotAPlugin.esp");
}

TEST_F(PluginsTxtTest, MissingFileReturnsEmpty) {
    auto lo = mora::LoadOrder::from_plugins_txt(
        tmp_dir / "does-not-exist.txt", tmp_dir);
    EXPECT_TRUE(lo.plugins.empty());
}

// ── RuntimeIndexMap::build ───────────────────────────────────────────

TEST(RuntimeIndexMapBuild, AssignsSequentialRegularIndices) {
    std::vector<mora::PluginOrderEntry> entries = {
        {"/data/Skyrim.esm",   "skyrim.esm",   false},
        {"/data/Update.esm",   "update.esm",   false},
        {"/data/Dawnguard.esm","dawnguard.esm",false},
    };

    auto map = mora::RuntimeIndexMap::build(entries);

    EXPECT_EQ(map.index.at("skyrim.esm"),    0u);
    EXPECT_EQ(map.index.at("update.esm"),    1u);
    EXPECT_EQ(map.index.at("dawnguard.esm"), 2u);
    EXPECT_TRUE(map.light.empty());
}

TEST(RuntimeIndexMapBuild, AssignsLightIndicesSeparately) {
    // Regular and ESL plugins share load-order *position* but use
    // independent index counters — regular plugins count 0..0xFD,
    // ESL plugins count 0..0xFFF under high byte 0xFE.
    std::vector<mora::PluginOrderEntry> entries = {
        {"/data/Skyrim.esm",  "skyrim.esm",  false},
        {"/data/SmallA.esl",  "smalla.esl",  true},
        {"/data/RegularB.esp","regularb.esp",false},
        {"/data/SmallC.esl",  "smallc.esl",  true},
    };

    auto map = mora::RuntimeIndexMap::build(entries);

    EXPECT_EQ(map.index.at("skyrim.esm"),    0u);
    EXPECT_EQ(map.index.at("regularb.esp"),  1u);
    EXPECT_EQ(map.index.at("smalla.esl"),    0u);   // first ESL
    EXPECT_EQ(map.index.at("smallc.esl"),    1u);   // second ESL
    EXPECT_EQ(map.light.count("smalla.esl"), 1u);
    EXPECT_EQ(map.light.count("smallc.esl"), 1u);
    EXPECT_EQ(map.light.count("skyrim.esm"), 0u);
}

// ── RuntimeIndexMap::globalize ───────────────────────────────────────

static mora::PluginInfo make_info(const std::string& filename,
                                  std::vector<std::string> masters,
                                  bool esl = false) {
    mora::PluginInfo info;
    info.filename = filename;
    info.masters = std::move(masters);
    info.flags = esl ? mora::RecordFlags::ESL : 0;
    return info;
}

TEST(RuntimeIndexMapGlobalize, SelfRefFromRegularPlugin) {
    // Dawnguard loads at runtime index 2 (implicit). A record defined
    // in Dawnguard has local form_id = (masters.size() << 24) | object_id.
    // After globalization the high byte must be Dawnguard's runtime
    // index, NOT its compile-time directory-walk position.
    mora::RuntimeIndexMap map;
    map.index["skyrim.esm"]    = 0;
    map.index["update.esm"]    = 1;
    map.index["dawnguard.esm"] = 2;

    auto info = make_info("Dawnguard.esm", {"Skyrim.esm", "Update.esm"});
    // high byte = 2 (self: masters.size() == 2)
    uint32_t local = (2u << 24) | 0x00ABCDEFu;

    uint32_t g = map.globalize(local, info);
    EXPECT_EQ(g, 0x02ABCDEFu);
}

TEST(RuntimeIndexMapGlobalize, ResolvesMasterReferences) {
    // A reference inside Dawnguard to a Skyrim.esm form has master
    // byte 0 (first MAST entry → Skyrim.esm). Runtime Skyrim is index
    // 0. Globalized formid must be 0x00<object_id>, not Dawnguard's.
    mora::RuntimeIndexMap map;
    map.index["skyrim.esm"]    = 0;
    map.index["dawnguard.esm"] = 1;

    auto info = make_info("Dawnguard.esm", {"Skyrim.esm"});
    uint32_t local = (0u << 24) | 0x00013476u; // master=0 → Skyrim
    uint32_t g = map.globalize(local, info);
    EXPECT_EQ(g, 0x00013476u);
}

TEST(RuntimeIndexMapGlobalize, EslPluginUsesFeHighByte) {
    // ESL plugin at ESL index 5 has records with local form_ids
    // carrying 12-bit object ids. Global must be 0xFE | (5 << 12) | oid.
    mora::RuntimeIndexMap map;
    map.index["skyrim.esm"] = 0;
    map.index["light.esl"]  = 5;
    map.light.insert("light.esl");

    auto info = make_info("Light.esl", {"Skyrim.esm"}, /*esl=*/true);
    // records in an ESL plugin use the self master byte
    uint32_t local = (1u << 24) | 0x123u;  // self, object_id 0x123
    uint32_t g = map.globalize(local, info);
    EXPECT_EQ(g, 0xFE005123u);
}

TEST(RuntimeIndexMapGlobalize, MasterByteBeyondListIsSelf) {
    // If the local form_id's master byte exceeds masters.size(),
    // treat it as the plugin itself. Mirrors runtime behavior: when a
    // plugin strips a master, stray high bytes collapse to "self".
    mora::RuntimeIndexMap map;
    map.index["alone.esp"] = 7;

    auto info = make_info("Alone.esp", {});  // no masters
    uint32_t local = (3u << 24) | 0x010101u; // master byte > 0 even though
                                             // masters is empty
    uint32_t g = map.globalize(local, info);
    EXPECT_EQ(g, 0x07010101u);
}

TEST(RuntimeIndexMapGlobalize, UnknownPluginFallsBackToIdentity) {
    // A plugin not listed in the runtime map (e.g., read ad-hoc)
    // should pass through its raw form_id rather than silently
    // corrupting it to zero or throwing.
    mora::RuntimeIndexMap map;
    auto info = make_info("Unknown.esp", {});
    uint32_t local = 0xAB123456u;
    uint32_t g = map.globalize(local, info);
    EXPECT_EQ(g, 0xAB123456u);
}

// ── Flag-driven classification (from_directory bucketing) ───────────

namespace {

// Write a minimal plugin file: a 24-byte TES4 record with the given
// raw TES4 flags (bit 0 = ESM, bit 9 = ESL) and zero data. `ext`
// selects the filename extension independently so we can exercise the
// "flag wins over extension" invariants.
fs::path write_minimal_plugin(const fs::path& dir,
                              const std::string& name,
                              uint32_t tes4_flags) {
    fs::create_directories(dir);
    auto path = dir / name;
    std::ofstream f(path, std::ios::binary);
    // RawRecordHeader layout (record_types.h): type[4] size[4] flags[4]
    //   form_id[4] timestamp[2] vcs_info[2] internal_version[2]
    //   unknown[2] = 24 bytes total.
    char hdr[24] = {};
    hdr[0] = 'T'; hdr[1] = 'E'; hdr[2] = 'S'; hdr[3] = '4';
    // data_size = 0 (bytes 4..7 stay zero)
    std::memcpy(hdr + 8, &tes4_flags, 4);
    // form_id, timestamps, version left zero
    f.write(hdr, sizeof(hdr));
    return path;
}

} // namespace

class FromDirectoryFlagsTest : public ::testing::Test {
protected:
    fs::path tmp_dir;
    void SetUp() override {
        tmp_dir = fs::temp_directory_path() /
                  ("mora_flags_" + std::to_string(::getpid()) + "_" +
                   std::to_string(::testing::UnitTest::GetInstance()
                       ->current_test_info()->name() ? 1 : 0));
        fs::create_directories(tmp_dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmp_dir, ec);
    }
};

TEST_F(FromDirectoryFlagsTest, EsPfEslBucketsWithMasters) {
    // An .esp file with the ESL flag set ("ESPFESL") loads with the
    // master group at runtime — plugin managers always sort it before
    // plain ESPs. from_directory approximates that by bucketing on
    // flags, not on extension.
    write_minimal_plugin(tmp_dir, "Skyrim.esm",   mora::RecordFlags::ESM);
    write_minimal_plugin(tmp_dir, "AnESPfESL.esp", mora::RecordFlags::ESL);
    write_minimal_plugin(tmp_dir, "ZPlainMod.esp", 0);

    auto lo = mora::LoadOrder::from_directory(tmp_dir);
    ASSERT_EQ(lo.plugins.size(), 3u);
    EXPECT_EQ(lo.plugins[0].filename(), "Skyrim.esm");
    EXPECT_EQ(lo.plugins[1].filename(), "AnESPfESL.esp");
    EXPECT_EQ(lo.plugins[2].filename(), "ZPlainMod.esp");
}

TEST_F(FromDirectoryFlagsTest, MasterFlagOnEspBucketsWithMasters) {
    // An .esp file with only the ESM flag (historically how some
    // mods ship "master" data without an .esm extension) still sorts
    // ahead of plain ESPs.
    write_minimal_plugin(tmp_dir, "Skyrim.esm",     mora::RecordFlags::ESM);
    write_minimal_plugin(tmp_dir, "ForcedMaster.esp", mora::RecordFlags::ESM);
    write_minimal_plugin(tmp_dir, "APlainMod.esp",  0);

    auto lo = mora::LoadOrder::from_directory(tmp_dir);
    ASSERT_EQ(lo.plugins.size(), 3u);
    EXPECT_EQ(lo.plugins[0].filename(), "Skyrim.esm");
    EXPECT_EQ(lo.plugins[1].filename(), "ForcedMaster.esp");
    EXPECT_EQ(lo.plugins[2].filename(), "APlainMod.esp");
}

TEST_F(FromDirectoryFlagsTest, EslExtensionWithoutEslFlagStillMaster) {
    // The engine keys on flags — a .esl file that somehow lacks the
    // ESL flag is still a master (its extension doesn't flip its
    // FormID space, but it still loads ahead of plain ESPs).
    write_minimal_plugin(tmp_dir, "Skyrim.esm",       mora::RecordFlags::ESM);
    write_minimal_plugin(tmp_dir, "UnflaggedEsl.esl", 0);
    write_minimal_plugin(tmp_dir, "APlainMod.esp",    0);

    auto lo = mora::LoadOrder::from_directory(tmp_dir);
    ASSERT_EQ(lo.plugins.size(), 3u);
    EXPECT_EQ(lo.plugins[0].filename(), "Skyrim.esm");
    EXPECT_EQ(lo.plugins[1].filename(), "UnflaggedEsl.esl");
    EXPECT_EQ(lo.plugins[2].filename(), "APlainMod.esp");
}

TEST_F(FromDirectoryFlagsTest, ResolveEntriesReportsBothFlags) {
    // resolve_entries must surface both is_master and is_esl so
    // downstream consumers (override filter, plugin/* facts) can
    // distinguish ESM / ESL / ESPFESL / ESM+ESL.
    write_minimal_plugin(tmp_dir, "OnlyMaster.esp", mora::RecordFlags::ESM);
    write_minimal_plugin(tmp_dir, "OnlyLight.esl",  mora::RecordFlags::ESL);
    write_minimal_plugin(tmp_dir, "PlainPlugin.esp", 0);
    write_minimal_plugin(tmp_dir, "Both.esp",
                          mora::RecordFlags::ESM | mora::RecordFlags::ESL);

    mora::LoadOrder lo;
    lo.data_dir = tmp_dir;
    lo.plugins = {tmp_dir / "OnlyMaster.esp",
                  tmp_dir / "OnlyLight.esl",
                  tmp_dir / "PlainPlugin.esp",
                  tmp_dir / "Both.esp"};
    auto entries = lo.resolve_entries();
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_TRUE(entries[0].is_master);
    EXPECT_FALSE(entries[0].is_esl);
    EXPECT_FALSE(entries[1].is_master);
    EXPECT_TRUE(entries[1].is_esl);
    EXPECT_FALSE(entries[2].is_master);
    EXPECT_FALSE(entries[2].is_esl);
    EXPECT_TRUE(entries[3].is_master);
    EXPECT_TRUE(entries[3].is_esl);
}

TEST(RuntimeIndexMapBuild, EspFeslLandsInLightSpace) {
    // Downstream of resolve_entries(): the RuntimeIndexMap builder
    // keys on is_esl only (light FormID space is strictly a flag
    // property, independent of whether the plugin is also a master).
    std::vector<mora::PluginOrderEntry> entries = {
        {"/data/Skyrim.esm",      "skyrim.esm",      /*is_esl=*/false, /*is_master=*/true},
        {"/data/AnEspFEsl.esp",   "anespfesl.esp",   /*is_esl=*/true,  /*is_master=*/false},
        {"/data/PlainPlugin.esp", "plainplugin.esp", /*is_esl=*/false, /*is_master=*/false},
    };
    auto map = mora::RuntimeIndexMap::build(entries);

    EXPECT_EQ(map.index.at("skyrim.esm"),      0u);
    EXPECT_EQ(map.index.at("plainplugin.esp"), 1u);     // regular idx 1
    EXPECT_EQ(map.index.at("anespfesl.esp"),   0u);     // light idx 0
    EXPECT_EQ(map.light.count("anespfesl.esp"), 1u);
    EXPECT_EQ(map.light.count("plainplugin.esp"), 0u);
}
