#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora::emit;

TEST(FlatFileWriter, EmptyFileHasHeaderAndZeroSections) {
    FlatFileWriter w;
    auto bytes = w.finish();
    ASSERT_GE(bytes.size(), sizeof(PatchFileV2Header));
    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.magic, 0x41524F4Du);
    EXPECT_EQ(h.version, 4u);
    EXPECT_EQ(h.section_count, 0u);
    EXPECT_EQ(h.file_size, bytes.size());
}

TEST(FlatFileWriter, SingleSectionRoundTrip) {
    FlatFileWriter w;
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    w.add_section(SectionId::Patches, payload, sizeof(payload));
    auto bytes = w.finish();

    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.section_count, 1u);
    EXPECT_EQ(h.file_size, bytes.size());

    SectionDirectoryEntry e;
    std::memcpy(&e, bytes.data() + sizeof(h), sizeof(e));
    EXPECT_EQ(e.section_id, static_cast<uint32_t>(SectionId::Patches));
    EXPECT_EQ(e.size, sizeof(payload));
    EXPECT_EQ(std::memcmp(bytes.data() + e.offset, payload, sizeof(payload)), 0);
}

TEST(FlatFileWriter, SectionsAreEightByteAligned) {
    FlatFileWriter w;
    const uint8_t p1[] = {1,2,3};
    const uint8_t p2[] = {4,5,6,7,8};
    w.add_section(SectionId::StringTable, p1, sizeof(p1));
    w.add_section(SectionId::Patches, p2, sizeof(p2));
    auto bytes = w.finish();

    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    ASSERT_EQ(h.section_count, 2u);

    SectionDirectoryEntry e0, e1;
    std::memcpy(&e0, bytes.data() + sizeof(h), sizeof(e0));
    std::memcpy(&e1, bytes.data() + sizeof(h) + sizeof(e0), sizeof(e1));
    EXPECT_EQ(e0.offset % 8, 0u);
    EXPECT_EQ(e1.offset % 8, 0u);
}

TEST(FlatFileWriter, EspDigestRoundTrips) {
    std::array<uint8_t, 32> d{};
    for (int i = 0; i < 32; ++i) d[i] = static_cast<uint8_t>(i);
    FlatFileWriter w;
    w.set_esp_digest(d);
    auto bytes = w.finish();
    PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.esp_digest, d);
}
