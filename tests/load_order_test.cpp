#include <gtest/gtest.h>
#include "mora/esp/load_order.h"
#include "mora/esp/plugin_index.h"
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
};

TEST_F(PluginsTxtTest, ReadsEnabledEntriesInOrder) {
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
}

TEST_F(PluginsTxtTest, ForcesSkyrimEsmFirst) {
    // If Plugins.txt lists Update.esm before Skyrim.esm (or omits
    // Skyrim.esm entirely), the engine still loads Skyrim.esm first.
    // Our parser must reflect that invariant.
    auto txt = write("Plugins.txt",
        "*Update.esm\n"
        "*Modish.esp\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);

    ASSERT_GE(lo.plugins.size(), 1u);
    EXPECT_EQ(lo.plugins.front().filename(), "Skyrim.esm");
}

TEST_F(PluginsTxtTest, TrimsCarriageReturns) {
    auto txt = write("Plugins.txt",
        "*Skyrim.esm\r\n"
        "*Mod.esp\r\n");

    auto lo = mora::LoadOrder::from_plugins_txt(txt, tmp_dir);
    ASSERT_EQ(lo.plugins.size(), 2u);
    EXPECT_EQ(lo.plugins[0].filename(), "Skyrim.esm");
    EXPECT_EQ(lo.plugins[1].filename(), "Mod.esp");
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
