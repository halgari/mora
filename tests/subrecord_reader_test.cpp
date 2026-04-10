#include <gtest/gtest.h>
#include "mora/esp/subrecord_reader.h"
#include "mora/esp/mmap_file.h"
#include "mora/esp/plugin_index.h"
#include <filesystem>

TEST(SubrecordReaderTest, ParseSyntheticRecord) {
    // Build a synthetic record data block:
    // EDID subrecord: "TestForm\0" (9 bytes)
    // DATA subrecord: 4 bytes of data
    std::vector<uint8_t> data;

    // EDID header: "EDID" + uint16(9)
    data.push_back('E'); data.push_back('D'); data.push_back('I'); data.push_back('D');
    data.push_back(9); data.push_back(0); // size = 9
    // EDID data: "TestForm\0"
    const char* edid = "TestForm";
    for (int i = 0; i < 9; i++) data.push_back(edid[i]);

    // DATA header: "DATA" + uint16(4)
    data.push_back('D'); data.push_back('A'); data.push_back('T'); data.push_back('A');
    data.push_back(4); data.push_back(0);
    // DATA data: 4 bytes
    data.push_back(0x01); data.push_back(0x02); data.push_back(0x03); data.push_back(0x04);

    mora::SubrecordReader reader(std::span<const uint8_t>(data), 0);

    mora::Subrecord sub;
    ASSERT_TRUE(reader.next(sub));
    EXPECT_TRUE(sub.type == "EDID");
    EXPECT_EQ(sub.data.size(), 9u);
    EXPECT_EQ(sub.data[0], 'T');

    ASSERT_TRUE(reader.next(sub));
    EXPECT_TRUE(sub.type == "DATA");
    EXPECT_EQ(sub.data.size(), 4u);
    EXPECT_EQ(sub.data[0], 0x01);

    EXPECT_FALSE(reader.next(sub));
}

TEST(SubrecordReaderTest, FindSubrecord) {
    std::vector<uint8_t> data;
    // EDID
    data.push_back('E'); data.push_back('D'); data.push_back('I'); data.push_back('D');
    data.push_back(5); data.push_back(0);
    data.push_back('T'); data.push_back('e'); data.push_back('s'); data.push_back('t'); data.push_back(0);
    // DATA
    data.push_back('D'); data.push_back('A'); data.push_back('T'); data.push_back('A');
    data.push_back(2); data.push_back(0);
    data.push_back(0xAA); data.push_back(0xBB);

    mora::SubrecordReader reader(std::span<const uint8_t>(data), 0);

    auto edid = reader.find("EDID");
    EXPECT_EQ(edid.size(), 5u);

    auto result = reader.find("DATA");
    EXPECT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 0xAA);

    auto missing = reader.find("XXXX");
    EXPECT_TRUE(missing.empty());
}

TEST(SubrecordReaderTest, FindAllRepeating) {
    std::vector<uint8_t> data;
    // Three SPLO subrecords (4 bytes each, representing spell FormIDs)
    for (int i = 0; i < 3; i++) {
        data.push_back('S'); data.push_back('P'); data.push_back('L'); data.push_back('O');
        data.push_back(4); data.push_back(0);
        uint32_t formid = 0x100 + i;
        data.push_back(formid & 0xFF); data.push_back((formid >> 8) & 0xFF);
        data.push_back(0); data.push_back(0);
    }

    mora::SubrecordReader reader(std::span<const uint8_t>(data), 0);
    auto all = reader.find_all("SPLO");
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].size(), 4u);
}

TEST(SubrecordReaderTest, XXXXExtendedSize) {
    // Build a record with an XXXX extended-size subrecord:
    // XXXX subrecord: size=4, value=65540 (larger than uint16 max)
    // DATA subrecord: size=0 in header (actual size from XXXX), 65540 bytes of data
    const uint32_t large_size = 65540;
    std::vector<uint8_t> data;

    // XXXX header: "XXXX" + uint16(4)
    data.push_back('X'); data.push_back('X'); data.push_back('X'); data.push_back('X');
    data.push_back(4); data.push_back(0);
    // XXXX data: uint32 = large_size
    data.push_back(large_size & 0xFF);
    data.push_back((large_size >> 8) & 0xFF);
    data.push_back((large_size >> 16) & 0xFF);
    data.push_back((large_size >> 24) & 0xFF);

    // DATA header: "DATA" + uint16(0) — actual size comes from XXXX
    data.push_back('D'); data.push_back('A'); data.push_back('T'); data.push_back('A');
    data.push_back(0); data.push_back(0);
    // DATA data: large_size bytes
    for (uint32_t i = 0; i < large_size; i++) {
        data.push_back(static_cast<uint8_t>(i & 0xFF));
    }

    mora::SubrecordReader reader(std::span<const uint8_t>(data), 0);

    mora::Subrecord sub;
    ASSERT_TRUE(reader.next(sub));
    EXPECT_TRUE(sub.type == "DATA");
    EXPECT_EQ(sub.data.size(), large_size);
    EXPECT_EQ(sub.data[0], 0x00);
    EXPECT_EQ(sub.data[1], 0x01);

    EXPECT_FALSE(reader.next(sub));
}

static const char* SKYRIM_ESM = "/home/tbaldrid/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/Skyrim.esm";

TEST(SubrecordReaderTest, RealSkyrimNPC) {
    if (!std::filesystem::exists(SKYRIM_ESM)) GTEST_SKIP();

    mora::MmapFile file(SKYRIM_ESM);
    auto info = mora::build_plugin_index(file, "Skyrim.esm");

    auto it = info.by_type.find("NPC_");
    ASSERT_NE(it, info.by_type.end());
    ASSERT_GT(it->second.size(), 0u);

    // Read the first NPC's subrecords
    auto& loc = it->second[0];
    auto record_data = file.span(loc.offset + sizeof(mora::RawRecordHeader), loc.data_size);
    mora::SubrecordReader reader(record_data, loc.flags);

    // Should find an EDID subrecord
    auto edid = reader.find("EDID");
    // Not all NPCs have EDID but most do
    if (!edid.empty()) {
        // EDID is a null-terminated string
        std::string_view name(reinterpret_cast<const char*>(edid.data()), edid.size() - 1);
        EXPECT_GT(name.size(), 0u);
    }
}
