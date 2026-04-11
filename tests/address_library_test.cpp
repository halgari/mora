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
    auto addr_lib = skyrim_data / "SKSE" / "Plugins" / "versionlib-1-5-97-0.bin";
    if (!std::filesystem::exists(addr_lib)) {
        auto plugins_dir = skyrim_data / "SKSE" / "Plugins";
        if (std::filesystem::exists(plugins_dir)) {
            for (auto& entry : std::filesystem::directory_iterator(plugins_dir)) {
                if (entry.path().filename().string().starts_with("versionlib")) {
                    addr_lib = entry.path();
                    break;
                }
            }
        }
    }
    if (!std::filesystem::exists(addr_lib)) GTEST_SKIP();

    mora::AddressLibrary lib;
    ASSERT_TRUE(lib.load(addr_lib));
    EXPECT_GT(lib.entry_count(), 100000u);

    auto offset = lib.resolve(514351);
    ASSERT_TRUE(offset.has_value());
    EXPECT_GT(*offset, 0u);
}
