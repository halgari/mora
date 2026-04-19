#include <gtest/gtest.h>
#include "mora_skyrim_compile/kid_parser.h"

#include <filesystem>
#include <fstream>

using namespace mora_skyrim_compile;

namespace {

KidLine parse_one(std::string_view s, std::vector<KidDiag>* diag_out = nullptr) {
    KidLine out;
    std::vector<KidDiag> diags;
    parse_kid_line(s, 1, out, diags);
    if (diag_out) *diag_out = std::move(diags);
    return out;
}

} // namespace

TEST(KidParserTest, BasicWeaponLine) {
    auto line = parse_one("MyKeyword|Weapon|IronKeyword|NONE|100");
    EXPECT_EQ(line.target.editor_id, "MyKeyword");
    EXPECT_EQ(line.item_type,        "weapon");
    ASSERT_EQ(line.filter.size(), 1u);
    ASSERT_EQ(line.filter[0].values.size(), 1u);
    EXPECT_EQ(line.filter[0].values[0].editor_id, "IronKeyword");
    EXPECT_DOUBLE_EQ(line.chance, 100.0);
}

TEST(KidParserTest, FormIdReference) {
    auto line = parse_one("0x1A2B~Custom.esp|Armor|NONE|NONE|NONE");
    EXPECT_TRUE(line.target.editor_id.empty());
    EXPECT_EQ(line.target.formid,   0x1A2Bu);
    EXPECT_EQ(line.target.mod_file, "Custom.esp");
    EXPECT_EQ(line.item_type,       "armor");
    EXPECT_TRUE(line.filter.empty());
}

TEST(KidParserTest, CommaIsOrPlusIsAnd) {
    // (ArmorHeavy AND ArmorGauntlet) OR Iron
    auto line = parse_one("KW|Armor|ArmorHeavy+ArmorGauntlet,Iron|NONE|100");
    ASSERT_EQ(line.filter.size(), 2u);
    ASSERT_EQ(line.filter[0].values.size(), 2u);
    EXPECT_EQ(line.filter[0].values[0].editor_id, "ArmorHeavy");
    EXPECT_EQ(line.filter[0].values[1].editor_id, "ArmorGauntlet");
    ASSERT_EQ(line.filter[1].values.size(), 1u);
    EXPECT_EQ(line.filter[1].values[0].editor_id, "Iron");
}

TEST(KidParserTest, TraitsEnchantedAndNeg) {
    auto line = parse_one("KW|Weapon|NONE|E,-E|100");
    ASSERT_EQ(line.traits.size(), 2u);
    EXPECT_EQ(line.traits[0], "E");
    EXPECT_EQ(line.traits[1], "-E");
}

TEST(KidParserTest, ChanceHalf) {
    auto line = parse_one("KW|Weapon|NONE|NONE|42.5");
    EXPECT_DOUBLE_EQ(line.chance, 42.5);
}

TEST(KidParserTest, KeywordEqPrefixStripped) {
    auto line = parse_one("Keyword = MyKW|Weapon|NONE|NONE|100");
    EXPECT_EQ(line.target.editor_id, "MyKW");
    EXPECT_EQ(line.item_type, "weapon");
}

TEST(KidParserTest, InlineComment) {
    auto line = parse_one("MyKW|Weapon|NONE|NONE|100 ; trailing comment");
    EXPECT_EQ(line.target.editor_id, "MyKW");
    EXPECT_DOUBLE_EQ(line.chance, 100.0);
}

TEST(KidParserTest, SkipsBlankAndComment) {
    KidLine out;
    std::vector<KidDiag> diags;
    EXPECT_FALSE(parse_kid_line("", 1, out, diags));
    EXPECT_FALSE(parse_kid_line("  ; full-line comment", 2, out, diags));
    EXPECT_FALSE(parse_kid_line("[Keywords]",             3, out, diags));
    EXPECT_TRUE(diags.empty());
}

TEST(KidParserTest, MalformedMissingFields) {
    std::vector<KidDiag> diags;
    auto line = parse_one("OnlyOneField", &diags);
    EXPECT_FALSE(diags.empty());
    EXPECT_NE(diags[0].message.find("at least 2"), std::string::npos);
    EXPECT_TRUE(line.item_type.empty());
}

TEST(KidParserTest, UnknownItemTypeDiagnosed) {
    std::vector<KidDiag> diags;
    auto line = parse_one("KW|SomethingBogus|NONE", &diags);
    EXPECT_FALSE(diags.empty());
    EXPECT_NE(diags[0].message.find("unknown KID item type"), std::string::npos);
    EXPECT_TRUE(line.item_type.empty());
}

TEST(KidParserTest, MagicEffectWithSpace) {
    auto line = parse_one("KW|Magic Effect|NONE|NONE|100");
    EXPECT_EQ(line.item_type, "magic_effect");
}

TEST(KidParserTest, TalkingActivatorMaps) {
    auto line = parse_one("KW|TalkingActivator|NONE|NONE|100");
    EXPECT_EQ(line.item_type, "talking_activator");
}

TEST(KidParserTest, ParseFileRoundtrip) {
    auto path = std::filesystem::temp_directory_path() / "mora_kid_parser_test_KID.ini";
    {
        std::ofstream o(path);
        o << "; comment\n";
        o << "[Keywords]\n";
        o << "KW1|Weapon|Iron|NONE|100\n";
        o << "\n";
        o << "KW2|Armor|Heavy+Boots,Light|E|50\n";
        o << "broken|unknown_type|NONE\n";   // should diag
    }
    auto file = parse_kid_file(path);
    EXPECT_EQ(file.lines.size(), 2u);
    EXPECT_FALSE(file.diags.empty());
    EXPECT_EQ(file.lines[0].target.editor_id, "KW1");
    EXPECT_EQ(file.lines[1].traits.size(), 1u);
    EXPECT_EQ(file.lines[1].traits[0], "E");
    std::filesystem::remove(path);
}
