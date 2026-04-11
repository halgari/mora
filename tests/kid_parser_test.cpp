#include <gtest/gtest.h>
#include "mora/import/kid_parser.h"
#include "mora/import/mora_printer.h"

class KidParserTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(KidParserTest, SimpleKeywordToWeapon) {
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line("Keyword = MyWeaponTag|Weapon|IronSword", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("weapon(Item)"), std::string::npos);
    EXPECT_NE(output.find("add_keyword"), std::string::npos);
    EXPECT_NE(output.find(":MyWeaponTag"), std::string::npos);
}

TEST_F(KidParserTest, KeywordToArmorByKeyword) {
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = HeavyGauntlets|Armor|ArmorHeavy+ArmorGauntlets", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("armor(Item)"), std::string::npos);
    EXPECT_NE(output.find("has_keyword"), std::string::npos);
}

TEST_F(KidParserTest, KeywordWithExclusion) {
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = NonEnchanted|Armor|ArmorHeavy,-ArmorEnchanted", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("not has_keyword"), std::string::npos);
}

TEST_F(KidParserTest, KeywordFromPlugin) {
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = 0x12345~MyMod.esp|Weapon|MyMod.esp", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
}

TEST_F(KidParserTest, ParseMultiLine) {
    std::string content =
        "; KID config\n"
        "Keyword = TagA|Weapon|IronSword\n"
        "Keyword = TagB|Armor|ArmorHeavy\n";
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_string(content, "test_KID.ini");
    ASSERT_EQ(rules.size(), 2u);
}

TEST_F(KidParserTest, SkipsComments) {
    mora::KidParser parser(pool, diags);
    auto rules = parser.parse_line("; comment", "test.ini", 1);
    EXPECT_TRUE(rules.empty());
}
