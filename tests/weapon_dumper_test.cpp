#include <gtest/gtest.h>
#include "mora/harness/weapon_dumper.h"
#include <cstring>
#include <sstream>

// Build a fake weapon form matching form_model.h's kWeaponSlots +
// kWeaponDirectMembers layout. total_size = 0x220.
struct FakeWeaponInit {
    uint32_t formid       = 0;
    uint16_t damage       = 0;
    int32_t  value        = 0;
    float    weight       = 0.0f;
    float    speed        = 0.0f;
    float    reach        = 0.0f;
    float    range_min    = 0.0f;
    float    range_max    = 0.0f;
    float    stagger      = 0.0f;
    uint16_t crit_damage  = 0;
};

static std::vector<uint8_t> make_fake_weapon(const FakeWeaponInit& init) {
    std::vector<uint8_t> buf(0x220, 0);

    // TESForm.formID at 0x14
    std::memcpy(buf.data() + 0x14, &init.formid, sizeof(init.formid));
    // TESForm.formType at 0x1A — Weapon = 0x29
    buf[0x1A] = 0x29;

    // Shared components
    std::memcpy(buf.data() + 0x0C0 + 0x08, &init.damage, sizeof(init.damage));
    std::memcpy(buf.data() + 0x0A0 + 0x08, &init.value,  sizeof(init.value));
    std::memcpy(buf.data() + 0x0B0 + 0x08, &init.weight, sizeof(init.weight));

    // WeaponDirect absolute offsets (kWeaponDirectMembers)
    std::memcpy(buf.data() + 0x170, &init.speed,       sizeof(init.speed));
    std::memcpy(buf.data() + 0x174, &init.reach,       sizeof(init.reach));
    std::memcpy(buf.data() + 0x178, &init.range_min,   sizeof(init.range_min));
    std::memcpy(buf.data() + 0x17C, &init.range_max,   sizeof(init.range_max));
    std::memcpy(buf.data() + 0x188, &init.stagger,     sizeof(init.stagger));
    std::memcpy(buf.data() + 0x1B0, &init.crit_damage, sizeof(init.crit_damage));

    return buf;
}

// Backward-compatible legacy overload used by older tests.
static std::vector<uint8_t> make_fake_weapon(uint32_t formid, uint16_t damage,
                                              int32_t value, float weight) {
    return make_fake_weapon(FakeWeaponInit{
        .formid = formid, .damage = damage, .value = value, .weight = weight,
    });
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

TEST(WeaponDumperTest, SerializeWeaponDirectFields) {
    auto buf = make_fake_weapon(FakeWeaponInit{
        .formid      = 0x00012EB7,
        .damage      = 7,
        .value       = 25,
        .weight      = 9.0f,
        .speed       = 1.0f,
        .reach       = 1.05f,
        .range_min   = 0.0f,
        .range_max   = 10.0f,
        .stagger     = 1.5f,
        .crit_damage = 2,
    });

    mora::harness::WeaponData data;
    mora::harness::read_weapon_fields(buf.data(), data);

    EXPECT_FLOAT_EQ(data.speed,     1.0f);
    EXPECT_FLOAT_EQ(data.reach,     1.05f);
    EXPECT_FLOAT_EQ(data.range_min, 0.0f);
    EXPECT_FLOAT_EQ(data.range_max, 10.0f);
    EXPECT_FLOAT_EQ(data.stagger,   1.5f);
    EXPECT_EQ     (data.crit_damage, 2);
    EXPECT_EQ     (data.enchantment_formid, 0u);  // no enchant pointer in synthetic data
}

TEST(WeaponDumperTest, JsonlIncludesAllScalars) {
    mora::harness::WeaponData data;
    data.formid      = 0x00012EB7;
    data.name        = "Iron Sword";
    data.damage      = 7;
    data.value       = 25;
    data.weight      = 9.0f;
    data.speed       = 1.0f;
    data.reach       = 1.05f;
    data.range_min   = 0.0f;
    data.range_max   = 10.0f;
    data.stagger     = 1.5f;
    data.crit_damage = 2;
    data.enchantment_formid = 0x000BE7E8;

    std::string line = mora::harness::weapon_to_jsonl(data);

    EXPECT_NE(line.find("\"speed\":1"),           std::string::npos);
    EXPECT_NE(line.find("\"reach\":1.05"),        std::string::npos);
    EXPECT_NE(line.find("\"range_min\":0"),       std::string::npos);
    EXPECT_NE(line.find("\"range_max\":10"),      std::string::npos);
    EXPECT_NE(line.find("\"stagger\":1.5"),       std::string::npos);
    EXPECT_NE(line.find("\"crit_damage\":2"),     std::string::npos);
    EXPECT_NE(line.find("\"enchantment\":\"0x000BE7E8\""), std::string::npos);
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
