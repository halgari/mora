#include "mora/emit/patch_table.h"
#include "mora/rt/mapped_patch_file.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

using namespace mora;

static std::filesystem::path write_temp(const std::vector<uint8_t>& bytes) {
    auto p = std::filesystem::temp_directory_path()
           / ("mora_pw_" + std::to_string(std::rand()) + ".bin");
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return p;
}

TEST(PatchWalkerV2, MappedFileExposesPatchesSection) {
    std::vector<PatchEntry> entries = {
        {0x12345u, 0, 0, 1, 0, 99u},
    };
    auto bytes = serialize_patch_table(entries);
    auto path = write_temp(bytes);

    rt::MappedPatchFile mpf;
    ASSERT_TRUE(mpf.open(path.string()));
    auto v = mpf.section(emit::SectionId::Patches);
    ASSERT_EQ(v.size, sizeof(PatchEntry));
    const PatchEntry* e = reinterpret_cast<const PatchEntry*>(v.data);
    EXPECT_EQ(e->formid, 0x12345u);
    EXPECT_EQ(e->value, 99u);

    std::filesystem::remove(path);
}
