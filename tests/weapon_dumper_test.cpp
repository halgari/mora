#include <gtest/gtest.h>
#include "mora/harness/weapon_dumper.h"
#include <cstring>
#include <sstream>

// Build a fake weapon form matching skyrim_abi.h weapon_offsets
// total_size = 0x220
static std::vector<uint8_t> make_fake_weapon(uint32_t formid, uint16_t damage,
                                              int32_t value, float weight) {
    std::vector<uint8_t> buf(0x220, 0);

    // TESForm.formID at 0x14
    std::memcpy(buf.data() + 0x14, &formid, sizeof(formid));
    // TESForm.formType at 0x1A — Weapon = 0x29
    buf[0x1A] = 0x29;

    // TESAttackDamageForm at 0x0C0, member at +0x08
    std::memcpy(buf.data() + 0x0C0 + 0x08, &damage, sizeof(damage));
    // TESValueForm at 0x0A0, member at +0x08
    std::memcpy(buf.data() + 0x0A0 + 0x08, &value, sizeof(value));
    // TESWeightForm at 0x0B0, member at +0x08
    std::memcpy(buf.data() + 0x0B0 + 0x08, &weight, sizeof(weight));

    return buf;
}

TEST(WeaponDumperTest, SerializeSingleWeapon) {
    auto buf = make_fake_weapon(0x00012EB7, 7, 25, 9.0f);

    mora::harness::WeaponData data;
    mora::harness::read_weapon_fields(buf.data(), data);

    EXPECT_EQ(data.formid, 0x00012EB7u);
    EXPECT_EQ(data.damage, 7);
    EXPECT_EQ(data.value, 25);
    EXPECT_FLOAT_EQ(data.weight, 9.0f);
    EXPECT_TRUE(data.keyword_formids.empty());
}

TEST(WeaponDumperTest, JsonlFormat) {
    mora::harness::WeaponData data;
    data.formid = 0x00012EB7;
    data.name = "Iron Sword";
    data.damage = 7;
    data.value = 25;
    data.weight = 9.0f;
    data.keyword_formids = {0x0006BBE3, 0x0001E711};

    std::string line = mora::harness::weapon_to_jsonl(data);

    // Keywords should be sorted
    EXPECT_NE(line.find("\"formid\":\"0x00012EB7\""), std::string::npos);
    EXPECT_NE(line.find("\"damage\":7"), std::string::npos);
    EXPECT_NE(line.find("\"value\":25"), std::string::npos);
    EXPECT_NE(line.find("\"name\":\"Iron Sword\""), std::string::npos);
    // 0x0001E711 should appear before 0x0006BBE3 (sorted)
    auto pos1 = line.find("0x0001E711");
    auto pos2 = line.find("0x0006BBE3");
    EXPECT_LT(pos1, pos2);
}

TEST(WeaponDumperTest, SortedByFormId) {
    std::vector<mora::harness::WeaponData> weapons;

    mora::harness::WeaponData w1;
    w1.formid = 0x200;
    w1.damage = 10; w1.value = 50; w1.weight = 5.0f;
    weapons.push_back(w1);

    mora::harness::WeaponData w2;
    w2.formid = 0x100;
    w2.damage = 20; w2.value = 100; w2.weight = 10.0f;
    weapons.push_back(w2);

    std::stringstream ss;
    mora::harness::write_weapons_jsonl(weapons, ss);

    std::string line1, line2;
    std::getline(ss, line1);
    std::getline(ss, line2);

    // 0x100 should come first
    EXPECT_NE(line1.find("\"formid\":\"0x00000100\""), std::string::npos);
    EXPECT_NE(line2.find("\"formid\":\"0x00000200\""), std::string::npos);
}
