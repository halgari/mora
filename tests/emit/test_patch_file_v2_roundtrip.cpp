#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora;

TEST(PatchFileV2Roundtrip, PatchesSectionContainsEntries) {
    std::vector<PatchEntry> entries = {
        {0x12345678u, 1, 0, 1, 0, 42u},
        {0x87654321u, 2, 1, 1, 0, 7u},
    };
    auto bytes = serialize_patch_table(entries);

    emit::PatchFileV2Header h;
    ASSERT_GE(bytes.size(), sizeof(h));
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.magic, 0x41524F4Du);
    EXPECT_EQ(h.version, 4u);
    EXPECT_GE(h.section_count, 1u);

    const emit::SectionDirectoryEntry* dir =
        reinterpret_cast<const emit::SectionDirectoryEntry*>(bytes.data() + sizeof(h));
    const emit::SectionDirectoryEntry* patches_entry = nullptr;
    for (uint32_t i = 0; i < h.section_count; ++i) {
        if (dir[i].section_id == static_cast<uint32_t>(emit::SectionId::Patches)) {
            patches_entry = &dir[i];
            break;
        }
    }
    ASSERT_NE(patches_entry, nullptr);
    EXPECT_EQ(patches_entry->size, entries.size() * sizeof(PatchEntry));

    EXPECT_EQ(std::memcmp(bytes.data() + patches_entry->offset,
                          entries.data(),
                          patches_entry->size), 0);
}

TEST(PatchFileV2Roundtrip, FileSizeMatchesHeader) {
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    auto bytes = serialize_patch_table(entries);
    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.file_size, bytes.size());
}
