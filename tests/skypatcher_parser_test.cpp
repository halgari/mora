#include <gtest/gtest.h>
#include "mora/import/skypatcher_parser.h"
#include "mora/import/mora_printer.h"

class SkyPatcherParserTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(SkyPatcherParserTest, WeaponDamage) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=WeaponTypeBow:attackDamage=30",
        "weapon", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("weapon(Weapon)"), std::string::npos);
    EXPECT_NE(output.find("has_keyword(Weapon, :WeaponTypeBow)"), std::string::npos);
    EXPECT_NE(output.find("set_damage(Weapon, 30)"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, ArmorKeywordAdd) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=ArmorHeavy:keywordsToAdd=MyTag",
        "armor", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("armor(Armor)"), std::string::npos);
    EXPECT_NE(output.find("add_keyword(Armor, :MyTag)"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, NPCSpellAdd) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByFactions=BanditFaction:spellsToAdd=FireBolt",
        "npc", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("npc(NPC)"), std::string::npos);
    EXPECT_NE(output.find("has_faction(NPC, :BanditFaction)"), std::string::npos);
    EXPECT_NE(output.find("add_spell(NPC, :FireBolt)"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, OrKeywordsEmitIn) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywordsOr=WeaponTypeSword,WeaponTypeAxe:attackDamage=25",
        "weapon", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("in ["), std::string::npos);
}

TEST_F(SkyPatcherParserTest, ExcludedKeywords) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=WeaponTypeBow:filterByKeywordsExcluded=WeapMaterialDaedric:attackDamage=20",
        "weapon", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("not has_keyword"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, MultipleOperations) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=ArmorHeavy:damageResist=50:weight=15:value=200",
        "armor", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("set_armor_rating"), std::string::npos);
    EXPECT_NE(output.find("set_weight"), std::string::npos);
    EXPECT_NE(output.find("set_gold_value"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, FormIDReference) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=Skyrim.esm|6D932:attackDamage=30",
        "weapon", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
}

TEST_F(SkyPatcherParserTest, TildeName) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByWeapons=IronSword:fullName=~Rusty Iron Sword~",
        "weapon", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("set_name"), std::string::npos);
    EXPECT_NE(output.find("Rusty Iron Sword"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, SkipsComments) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line("; comment line", "weapon", "test.ini", 1);
    EXPECT_TRUE(rules.empty());
}

TEST_F(SkyPatcherParserTest, SkipsNoEffects) {
    // A line with only filters and no operations should produce no rules
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=ArmorHeavy",
        "armor", "test.ini", 1);
    EXPECT_TRUE(rules.empty());
}

TEST_F(SkyPatcherParserTest, NPCLevelRange) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "levelRange=10~50:keywordsToAdd=MidLevel",
        "npc", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    EXPECT_NE(output.find("base_level"), std::string::npos);
}

TEST_F(SkyPatcherParserTest, MultipleKeywordsToAdd) {
    mora::SkyPatcherParser parser(pool, diags);
    auto rules = parser.parse_line(
        "filterByKeywords=ArmorHeavy:keywordsToAdd=TagA,TagB,TagC",
        "armor", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    auto output = mora::MoraPrinter(pool).print_rule(rules[0]);
    // Should have 3 separate add_keyword effects
    size_t count = 0;
    size_t pos = 0;
    while ((pos = output.find("add_keyword", pos)) != std::string::npos) { count++; pos++; }
    EXPECT_EQ(count, 3u);
}
