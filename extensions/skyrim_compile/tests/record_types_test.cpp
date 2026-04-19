#include <gtest/gtest.h>
#include "mora_skyrim_compile/esp/record_types.h"
#include "mora_skyrim_compile/esp/mmap_file.h"
#include <filesystem>

TEST(RecordTypesTest, RecordTagComparison) {
    auto tag = mora::RecordTag::from("NPC_");
    EXPECT_TRUE(tag == "NPC_");
    EXPECT_FALSE(tag == "WEAP");
    EXPECT_EQ(tag.as_sv(), "NPC_");
}

TEST(RecordTypesTest, HeaderSizes) {
    EXPECT_EQ(sizeof(mora::RawRecordHeader), 24u);
    EXPECT_EQ(sizeof(mora::RawGrupHeader), 24u);
    EXPECT_EQ(sizeof(mora::RawSubrecordHeader), 6u);
}

TEST(RecordTypesTest, CastFromBytes) {
    // Simulate a record header in memory
    uint8_t data[24] = {};
    data[0] = 'W'; data[1] = 'E'; data[2] = 'A'; data[3] = 'P'; // type
    // data_size = 100 (little-endian)
    data[4] = 100; data[5] = 0; data[6] = 0; data[7] = 0;
    // form_id = 0x00012EB7
    data[12] = 0xB7; data[13] = 0x2E; data[14] = 0x01; data[15] = 0x00;

    auto* header = mora::read_record_header(data);
    EXPECT_TRUE(header->type == "WEAP");
    EXPECT_EQ(header->data_size, 100u);
    EXPECT_EQ(header->form_id, 0x00012EB7u);
}

TEST(RecordTypesTest, ReadRealSkyrimHeader) {
    const char* path = "/home/tbaldrid/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";
    if (std::filesystem::exists(path)) {
        mora::MmapFile file(path);
        auto data = file.span();
        auto* header = mora::read_record_header(data.data());
        EXPECT_TRUE(header->type == "TES4");
        EXPECT_GT(header->data_size, 0u);
        // TES4 should have ESM + LOCALIZED flags for Skyrim.esm
        EXPECT_TRUE(header->flags & mora::RecordFlags::ESM);
    }
}
