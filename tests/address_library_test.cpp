#include "mora/codegen/address_library.h"

#include <gtest/gtest.h>

#include <filesystem>

TEST(AddressLibraryTest, MockLibrary) {
    auto lib = mora::AddressLibrary::mock({
        {514351, 0x1EEBE10},
        {514352, 0x1EEBE30},
    });
    EXPECT_EQ(lib.entry_count(), 2u);
    auto offset = lib.resolve(514351);
    ASSERT_TRUE(offset.has_value());
    EXPECT_EQ(*offset, 0x1EEBE10u);
    EXPECT_FALSE(lib.resolve(999999).has_value());
}

TEST(AddressLibraryTest, RealFile) {
    std::filesystem::path skyrim_data =
        "/home/tbaldrid/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data";

    // Prefer the exact 1170 file
    auto addr_lib = skyrim_data / "SKSE" / "Plugins" / "versionlib-1-6-1170-0.bin";
    if (!std::filesystem::exists(addr_lib)) {
        addr_lib = skyrim_data / "SKSE" / "Plugins" / "version-1-5-97-0.bin";
    }
    if (!std::filesystem::exists(addr_lib)) GTEST_SKIP();

    mora::AddressLibrary lib;
    ASSERT_TRUE(lib.load(addr_lib));
    EXPECT_GT(lib.entry_count(), 100000u);

    // AE 1.6.1170: ID 400507 -> 0x20FBB88
    auto offset = lib.resolve(400507);
    if (offset) {
        EXPECT_EQ(*offset, 0x20FBB88u)
            << "Expected AE allForms offset 0x20FBB88, got 0x" << std::hex << *offset;
    } else {
        // SE fallback
        offset = lib.resolve(514351);
        ASSERT_TRUE(offset.has_value()) << "Neither AE (400507) nor SE (514351) ID found";
    }
    EXPECT_GT(*offset, 0u);
}
