#include <gtest/gtest.h>
#include "mora_skyrim_compile/esp/plugin_index.h"
#include "skyrim_fixture.h"
#include <filesystem>
#include <string>

static std::string skyrim_esm() {
    return mora::test::skyrim_esm_path().string();
}

class PluginIndexTest : public ::testing::Test {};

TEST_F(PluginIndexTest, SkyrimEsmHeader) {

    mora::MmapFile file(skyrim_esm());
    auto info = mora::build_plugin_index(file, "Skyrim.esm");

    EXPECT_EQ(info.filename, "Skyrim.esm");
    EXPECT_TRUE(info.is_esm());
    EXPECT_TRUE(info.is_localized());
    EXPECT_GT(info.version, 0.0f);
    EXPECT_GT(info.num_records, 0u);
    // Skyrim.esm has no masters
    EXPECT_TRUE(info.masters.empty());
}

TEST_F(PluginIndexTest, SkyrimHasNPCs) {

    mora::MmapFile file(skyrim_esm());
    auto info = mora::build_plugin_index(file, "Skyrim.esm");

    auto it = info.by_type.find("NPC_");
    ASSERT_NE(it, info.by_type.end());
    // Skyrim.esm has thousands of NPCs
    EXPECT_GT(it->second.size(), 1000u);
}

TEST_F(PluginIndexTest, SkyrimHasWeapons) {

    mora::MmapFile file(skyrim_esm());
    auto info = mora::build_plugin_index(file, "Skyrim.esm");

    auto it = info.by_type.find("WEAP");
    ASSERT_NE(it, info.by_type.end());
    EXPECT_GT(it->second.size(), 100u);
}

TEST_F(PluginIndexTest, SkyrimHasKeywords) {

    mora::MmapFile file(skyrim_esm());
    auto info = mora::build_plugin_index(file, "Skyrim.esm");

    auto it = info.by_type.find("KYWD");
    ASSERT_NE(it, info.by_type.end());
    EXPECT_GT(it->second.size(), 100u);
}

TEST_F(PluginIndexTest, FormIDResolution) {
    mora::PluginInfo info;
    info.filename = "MyMod.esp";
    info.masters = {"Skyrim.esm", "Dawnguard.esm"};

    // FormID 0x00012345 -> master 0 = Skyrim.esm, object 0x012345
    auto r1 = mora::resolve_form_id(0x00012345, info);
    EXPECT_EQ(r1.master, "Skyrim.esm");
    EXPECT_EQ(r1.object_id, 0x012345u);

    // FormID 0x01AABBCC -> master 1 = Dawnguard.esm
    auto r2 = mora::resolve_form_id(0x01AABBCC, info);
    EXPECT_EQ(r2.master, "Dawnguard.esm");
    EXPECT_EQ(r2.object_id, 0x00AABBCCu);

    // FormID 0x02001234 -> self (MyMod.esp)
    auto r3 = mora::resolve_form_id(0x02001234, info);
    EXPECT_EQ(r3.master, "MyMod.esp");
}

TEST_F(PluginIndexTest, RecordTypeCounts) {

    mora::MmapFile file(skyrim_esm());
    auto info = mora::build_plugin_index(file, "Skyrim.esm");

    // Print some record type counts for verification
    size_t total = 0;
    for (auto& [type, records] : info.by_type) {
        total += records.size();
    }
    EXPECT_GT(total, 10000u);
    // Skyrim.esm should have many distinct record types
    EXPECT_GT(info.by_type.size(), 50u);
}
