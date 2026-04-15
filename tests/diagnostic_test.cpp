#include <gtest/gtest.h>
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"

TEST(DiagnosticTest, CreateError) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Error;
    diag.code = "E012";
    diag.message = "type mismatch";
    diag.span = {"weapons.mora", 14, 21, 14, 31};
    diag.source_line = "    has_faction(NPC, :IronSword)";
    EXPECT_EQ(diag.level, mora::DiagLevel::Error);
    EXPECT_EQ(diag.code, "E012");
}

TEST(DiagnosticTest, CreateWarning) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Warning;
    diag.code = "W003";
    diag.message = "rule matches 0 records";
    EXPECT_EQ(diag.level, mora::DiagLevel::Warning);
}

TEST(DiagnosticTest, AddNotes) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Error;
    diag.code = "E012";
    diag.message = "type mismatch";
    diag.span = {"weapons.mora", 14, 21, 14, 31};
    diag.source_line = "    has_faction(NPC, :IronSword)";
    diag.notes.push_back(":IronSword is Skyrim.esm|0x00012EB7 (weapon)");
    diag.notes.push_back("did you mean :BanditFaction?");
    EXPECT_EQ(diag.notes.size(), 2u);
}

TEST(DiagnosticTest, DiagBagCollectsMultiple) {
    mora::DiagBag bag;
    bag.error("E001", "first error", {"test.mora", 1, 1, 1, 5}, "line1");
    bag.error("E002", "second error", {"test.mora", 2, 1, 2, 5}, "line2");
    bag.warning("W001", "a warning", {"test.mora", 3, 1, 3, 5}, "line3");
    EXPECT_EQ(bag.error_count(), 2u);
    EXPECT_EQ(bag.warning_count(), 1u);
    EXPECT_TRUE(bag.has_errors());
    EXPECT_EQ(bag.all().size(), 3u);
}

TEST(DiagnosticRendererTest, RenderErrorPlainText) {
    mora::Diagnostic diag;
    diag.level = mora::DiagLevel::Error;
    diag.code = "E012";
    diag.message = "type mismatch";
    diag.span = {"weapons.mora", 14, 21, 14, 31};
    diag.source_line = "    has_faction(NPC, :IronSword)";
    diag.notes.push_back("expected FactionID, found WeaponID");

    mora::DiagRenderer renderer(/*use_color=*/false);
    std::string output = renderer.render(diag);

    EXPECT_NE(output.find("E012"), std::string::npos);
    EXPECT_NE(output.find("weapons.mora:14:21"), std::string::npos);
    EXPECT_NE(output.find("has_faction"), std::string::npos);
    EXPECT_NE(output.find("expected FactionID"), std::string::npos);
}

TEST(DiagBag, DrainForFileReturnsOnlyMatching) {
    using namespace mora;
    DiagBag bag;
    SourceSpan a{}; a.file = "/home/u/a.mora";
    SourceSpan b{}; b.file = "/home/u/b.mora";
    bag.error("E001", "in a", a, "");
    bag.error("E001", "in b", b, "");
    bag.error("E001", "also a", a, "");

    auto for_a = bag.drain_for_file("/home/u/a.mora");
    EXPECT_EQ(for_a.size(), 2u);
    EXPECT_EQ(for_a[0].message, "in a");
    EXPECT_EQ(for_a[1].message, "also a");

    // After drain, b's diagnostics remain in the bag.
    auto for_b = bag.drain_for_file("/home/u/b.mora");
    EXPECT_EQ(for_b.size(), 1u);
}
