#include <gtest/gtest.h>
#include "mora/harness/armor_dumper.h"
#include <cstring>
#include <sstream>

namespace {

// Layout: include/mora/data/form_model.h kArmorSlots + kArmorDirectMembers.
// armorRating lives at absolute +0x200, so allocate ≥ 0x210 bytes (with a
// pad for fake form stubs past the ARMO footprint).
static std::vector<uint8_t> make_fake_armor(uint32_t formid, int32_t value,
                                             float weight, uint32_t armor_rating,
                                             uint32_t ench_formid = 0) {
    std::vector<uint8_t> buf(0x240, 0);
    std::memcpy(buf.data() + 0x14, &formid, sizeof(formid));
    buf[0x1A] = 0x1A;  // ARMO formType

    std::memcpy(buf.data() + 0x068 + 0x08, &value,        sizeof(value));
    std::memcpy(buf.data() + 0x078 + 0x08, &weight,       sizeof(weight));
    std::memcpy(buf.data() + 0x200,        &armor_rating, sizeof(armor_rating));

    // Fake enchantment stub at 0x220.
    if (ench_formid != 0) {
        std::memcpy(buf.data() + 0x220 + 0x14, &ench_formid, sizeof(ench_formid));
        void* ench_ptr = buf.data() + 0x220;
        std::memcpy(buf.data() + 0x050 + 0x08, &ench_ptr, sizeof(ench_ptr));
    }
    return buf;
}

TEST(ArmorDumperTest, SerializeScalars) {
    auto buf = make_fake_armor(0x00012E4D, 60, 5.0f, 1500);

    mora::harness::ArmorData data;
    mora::harness::read_armor_fields(buf.data(), data);

    EXPECT_EQ(data.formid,       0x00012E4Du);
    EXPECT_EQ(data.value,        60);
    EXPECT_FLOAT_EQ(data.weight, 5.0f);
    EXPECT_EQ(data.armor_rating, 1500u);
    EXPECT_EQ(data.enchantment_formid, 0u);
}

TEST(ArmorDumperTest, SerializeEnchanted) {
    auto buf = make_fake_armor(0x00012E4D, 60, 5.0f, 1500, 0x000BE7E8);

    mora::harness::ArmorData data;
    mora::harness::read_armor_fields(buf.data(), data);

    EXPECT_EQ(data.enchantment_formid, 0x000BE7E8u);
}

TEST(ArmorDumperTest, JsonlIncludesAllFields) {
    mora::harness::ArmorData d;
    d.formid = 0x00012E4D;
    d.name   = "Iron Helmet";
    d.value  = 60;
    d.weight = 5.0f;
    d.armor_rating = 1500;
    d.enchantment_formid = 0x000BE7E8;
    d.keyword_formids = {0x0006BBD7, 0x0006C0EC};

    auto s = mora::harness::armor_to_jsonl(d);
    EXPECT_NE(s.find("\"formid\":\"0x00012E4D\""),      std::string::npos);
    EXPECT_NE(s.find("\"name\":\"Iron Helmet\""),       std::string::npos);
    EXPECT_NE(s.find("\"value\":60"),                   std::string::npos);
    EXPECT_NE(s.find("\"weight\":5"),                   std::string::npos);
    EXPECT_NE(s.find("\"armor_rating\":1500"),          std::string::npos);
    EXPECT_NE(s.find("\"enchantment\":\"0x000BE7E8\""), std::string::npos);
    // Keywords sorted.
    auto pos_a = s.find("0x0006BBD7");
    auto pos_b = s.find("0x0006C0EC");
    EXPECT_NE(pos_a, std::string::npos);
    EXPECT_NE(pos_b, std::string::npos);
    EXPECT_LT(pos_a, pos_b);
}

TEST(ArmorDumperTest, SortedByFormId) {
    std::vector<mora::harness::ArmorData> armors;
    mora::harness::ArmorData a; a.formid = 0x200; armors.push_back(a);
    mora::harness::ArmorData b; b.formid = 0x100; armors.push_back(b);

    std::stringstream ss;
    mora::harness::write_armors_jsonl(armors, ss);
    std::string line1, line2;
    std::getline(ss, line1);
    std::getline(ss, line2);
    EXPECT_NE(line1.find("\"formid\":\"0x00000100\""), std::string::npos);
    EXPECT_NE(line2.find("\"formid\":\"0x00000200\""), std::string::npos);
}

} // namespace
