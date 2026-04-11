#include <gtest/gtest.h>
#include "mora/harness/ini_reader.h"
#include <fstream>
#include <filesystem>

TEST(IniReaderTest, ParsePortAndDumpPath) {
    auto path = std::filesystem::temp_directory_path() / "test_harness.ini";
    {
        std::ofstream f(path);
        f << "[General]\n"
          << "port=9742\n"
          << "dump_path=Data/MoraCache/dumps\n";
    }

    auto config = mora::harness::read_ini(path);
    EXPECT_EQ(config.port, 9742);
    EXPECT_EQ(config.dump_path, "Data/MoraCache/dumps");
    std::filesystem::remove(path);
}

TEST(IniReaderTest, DefaultsWhenMissing) {
    auto path = std::filesystem::temp_directory_path() / "empty_harness.ini";
    {
        std::ofstream f(path);
        f << "[General]\n";
    }

    auto config = mora::harness::read_ini(path);
    EXPECT_EQ(config.port, 9742);
    EXPECT_EQ(config.dump_path, "Data/MoraCache/dumps");
    std::filesystem::remove(path);
}

TEST(IniReaderTest, DefaultsWhenFileNotFound) {
    auto config = mora::harness::read_ini("/tmp/nonexistent_harness.ini");
    EXPECT_EQ(config.port, 9742);
    EXPECT_EQ(config.dump_path, "Data/MoraCache/dumps");
}

TEST(IniReaderTest, IgnoresComments) {
    auto path = std::filesystem::temp_directory_path() / "comment_harness.ini";
    {
        std::ofstream f(path);
        f << "; this is a comment\n"
          << "[General]\n"
          << "# another comment\n"
          << "port=1234\n";
    }

    auto config = mora::harness::read_ini(path);
    EXPECT_EQ(config.port, 1234);
    std::filesystem::remove(path);
}
