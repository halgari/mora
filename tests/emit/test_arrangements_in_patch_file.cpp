#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include "mora/emit/arrangement_emit.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora;

TEST(ArrangementsInPatchFile, ArrangementsSectionIsPresent) {
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    auto a = emit::build_u32_arrangement(0,
        std::vector<std::array<uint32_t, 2>>{{{0x10, 0x20}}}, 0);
    auto section = emit::build_arrangements_section({a});

    std::array<uint8_t, 32> zero{};
    auto bytes = serialize_patch_table(entries, zero, section);

    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));

    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    bool found = false;
    for (uint32_t i = 0; i < h.section_count; ++i) {
        if (dir[i].section_id == static_cast<uint32_t>(emit::SectionId::Arrangements)) {
            found = true;
            EXPECT_EQ(dir[i].size, section.size());
            EXPECT_EQ(std::memcmp(bytes.data() + dir[i].offset,
                                  section.data(), section.size()), 0);
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ArrangementsInPatchFile, EmptyArrangementsOmitsSection) {
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    std::array<uint8_t, 32> zero{};
    auto bytes = serialize_patch_table(entries, zero, /*arrangements*/ std::vector<uint8_t>{});
    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));

    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    for (uint32_t i = 0; i < h.section_count; ++i) {
        EXPECT_NE(dir[i].section_id, static_cast<uint32_t>(emit::SectionId::Arrangements));
    }
}
