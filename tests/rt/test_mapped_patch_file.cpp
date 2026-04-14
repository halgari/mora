#include "mora/rt/mapped_patch_file.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace mora;
using namespace mora::emit;
using namespace mora::rt;

static std::filesystem::path write_temp_file(const std::vector<uint8_t>& bytes) {
    auto path = std::filesystem::temp_directory_path()
              / ("mora_test_" + std::to_string(std::rand()) + ".bin");
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

TEST(MappedPatchFile, OpenAndFindPatchesSection) {
    FlatFileWriter w;
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    w.add_section(SectionId::Patches, payload, sizeof(payload));
    auto bytes = w.finish();
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    auto view = mpf.section(SectionId::Patches);
    ASSERT_NE(view.data, nullptr);
    EXPECT_EQ(view.size, sizeof(payload));
    EXPECT_EQ(view.data[0], 0xAAu);
    EXPECT_EQ(view.data[3], 0xDDu);

    std::filesystem::remove(path);
}

TEST(MappedPatchFile, MissingSectionReturnsNullView) {
    FlatFileWriter w;
    auto bytes = w.finish();
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    auto view = mpf.section(SectionId::Patches);
    EXPECT_EQ(view.data, nullptr);
    EXPECT_EQ(view.size, 0u);

    std::filesystem::remove(path);
}

TEST(MappedPatchFile, BadMagicFailsOpen) {
    std::vector<uint8_t> bytes(sizeof(PatchFileV2Header), 0xFF);
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    EXPECT_FALSE(mpf.open(path.string()));

    std::filesystem::remove(path);
}

TEST(MappedPatchFile, HeaderExposed) {
    std::array<uint8_t, 32> d{};
    d[7] = 0x77;
    FlatFileWriter w;
    w.set_esp_digest(d);
    auto bytes = w.finish();
    auto path = write_temp_file(bytes);

    MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    EXPECT_EQ(mpf.header().esp_digest, d);

    std::filesystem::remove(path);
}
