#include <gtest/gtest.h>
#include "mora/harness/leveled_list_dumper.h"
#include <cstring>
#include <sstream>

namespace {

// Layout: include/mora/data/form_model.h kLeveledItemSlots +
// kLeveledListMembers. TESLeveledList component at +0x030,
// chanceNone at +0x10 → absolute 0x040.
static std::vector<uint8_t> make_fake_leveled_list(uint32_t formid, int8_t chance_none,
                                                    uint8_t form_type = 0x2D) {
    std::vector<uint8_t> buf(0x80, 0);
    std::memcpy(buf.data() + 0x14, &formid, sizeof(formid));
    buf[0x1A] = form_type;
    std::memcpy(buf.data() + 0x040, &chance_none, sizeof(chance_none));
    return buf;
}

TEST(LeveledListDumperTest, ReadChanceNone) {
    auto buf = make_fake_leveled_list(0x00000EDE, 25);

    mora::harness::LeveledListData data;
    mora::harness::read_leveled_list_fields(buf.data(), data);

    EXPECT_EQ(data.formid,      0x00000EDEu);
    EXPECT_EQ(data.chance_none, 25);
    EXPECT_TRUE(data.entries.empty());  // raw-memcpy path leaves entries empty
}

TEST(LeveledListDumperTest, JsonlEmptyEntries) {
    mora::harness::LeveledListData d;
    d.formid = 0x00000EDE;
    d.chance_none = 25;

    auto s = mora::harness::leveled_list_to_jsonl(d);
    EXPECT_NE(s.find("\"formid\":\"0x00000EDE\""), std::string::npos);
    EXPECT_NE(s.find("\"chance_none\":25"),        std::string::npos);
    EXPECT_NE(s.find("\"entries\":[]"),            std::string::npos);
}

TEST(LeveledListDumperTest, JsonlNonEmptyEntries) {
    mora::harness::LeveledListData d;
    d.formid = 0x00000EDE;
    d.chance_none = 0;
    d.entries = {{1, 0x00012EB7, 1}, {10, 0x00013989, 2}};

    auto s = mora::harness::leveled_list_to_jsonl(d);
    EXPECT_NE(s.find("\"level\":1"),              std::string::npos);
    EXPECT_NE(s.find("\"form\":\"0x00012EB7\""),  std::string::npos);
    EXPECT_NE(s.find("\"count\":1"),              std::string::npos);
    EXPECT_NE(s.find("\"level\":10"),             std::string::npos);
    EXPECT_NE(s.find("\"form\":\"0x00013989\""),  std::string::npos);
    EXPECT_NE(s.find("\"count\":2"),              std::string::npos);
}

TEST(LeveledListDumperTest, SortedByFormId) {
    std::vector<mora::harness::LeveledListData> lists;
    mora::harness::LeveledListData a; a.formid = 0x200; lists.push_back(a);
    mora::harness::LeveledListData b; b.formid = 0x100; lists.push_back(b);

    std::stringstream ss;
    mora::harness::write_leveled_lists_jsonl(lists, ss);
    std::string line1, line2;
    std::getline(ss, line1);
    std::getline(ss, line2);
    EXPECT_NE(line1.find("\"formid\":\"0x00000100\""), std::string::npos);
    EXPECT_NE(line2.find("\"formid\":\"0x00000200\""), std::string::npos);
}

} // namespace
