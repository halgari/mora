#include <gtest/gtest.h>
#include "mora_skyrim_compile/esp/mmap_file.h"
#include <fstream>
#include <filesystem>

class MmapFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::ofstream out("test_mmap.bin", std::ios::binary);
        uint8_t data[] = {'T','E','S','4','h','e','l','l','o',' ','w','o','r','l','d'};
        out.write(reinterpret_cast<const char*>(data), sizeof(data));
    }
    void TearDown() override {
        std::filesystem::remove("test_mmap.bin");
    }
};

TEST_F(MmapFileTest, OpenAndRead) {
    mora::MmapFile file("test_mmap.bin");
    EXPECT_EQ(file.size(), 15u);
    auto data = file.span();
    EXPECT_EQ(data[0], 'T');
    EXPECT_EQ(data[1], 'E');
    EXPECT_EQ(data[2], 'S');
    EXPECT_EQ(data[3], '4');
}

TEST_F(MmapFileTest, SubSpan) {
    mora::MmapFile file("test_mmap.bin");
    auto sub = file.span(4, 5);
    EXPECT_EQ(sub.size(), 5u);
    EXPECT_EQ(sub[0], 'h');
    EXPECT_EQ(sub[4], 'o');
}

TEST_F(MmapFileTest, NonExistentFile) {
    mora::MmapFile f("nonexistent.bin");
    EXPECT_EQ(f.size(), 0u);
}

TEST_F(MmapFileTest, RealSkyrimEsm) {
    // Test against actual Skyrim.esm if available
    const char* path = "/home/tbaldrid/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    if (std::filesystem::exists(path)) {
        mora::MmapFile file(path);
        EXPECT_GT(file.size(), 200'000'000u); // ~250MB
        auto data = file.span();
        // First 4 bytes should be "TES4"
        EXPECT_EQ(data[0], 'T');
        EXPECT_EQ(data[1], 'E');
        EXPECT_EQ(data[2], 'S');
        EXPECT_EQ(data[3], '4');
    }
}
