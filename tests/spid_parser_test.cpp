#include <gtest/gtest.h>
#include "mora/import/spid_parser.h"
#include "mora/import/mora_printer.h"

class SpidParserTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(SpidParserTest, SimpleKeyword) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line("Keyword = ActorTypePoor|Brenuin", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_keyword"), std::string::npos);
    EXPECT_NE(output.find(":ActorTypePoor"), std::string::npos);
}

TEST_F(SpidParserTest, SpellWithFactionFilter) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Spell = 0x12FCD~Skyrim.esm|NONE|CrimeFactionWhiterun", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_spell"), std::string::npos);
    EXPECT_NE(output.find("has_faction"), std::string::npos);
}

TEST_F(SpidParserTest, ItemWithKeywordFilter) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Item = 0xF~Skyrim.esm|ActorTypeNPC|NONE|NONE|NONE|3000", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_item"), std::string::npos);
}

TEST_F(SpidParserTest, KeywordWithExclusion) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line(
        "Keyword = ActorTypeWarrior|ActorTypeNPC,-Nazeem", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("not"), std::string::npos);
}

TEST_F(SpidParserTest, WithLevelFilter) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line("Spell = FireBolt|NONE|NONE|25/50|F", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("base_level"), std::string::npos);
}

TEST_F(SpidParserTest, PerkDistribution) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line("Perk = LightFoot|NONE|NONE|NONE|NONE|NONE|50", "test.ini", 1);
    ASSERT_EQ(rules.size(), 1u);
    mora::MoraPrinter printer(pool);
    auto output = printer.print_rule(rules[0]);
    EXPECT_NE(output.find("add_perk"), std::string::npos);
}

TEST_F(SpidParserTest, ParseMultiLine) {
    std::string content =
        "; Comment\n"
        "Keyword = ActorTypePoor|Brenuin\n"
        "\n"
        "Spell = FireBolt|ActorTypeNPC\n";
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_string(content, "test_DISTR.ini");
    ASSERT_EQ(rules.size(), 2u);
}

TEST_F(SpidParserTest, SkipsComments) {
    mora::SpidParser parser(pool, diags);
    auto rules = parser.parse_line("; comment", "test.ini", 1);
    EXPECT_TRUE(rules.empty());
}
