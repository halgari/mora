#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mora/lsp/diagnostics_convert.h"
#include "mora/diag/diagnostic.h"
#include "mora/core/source_location.h"

using namespace mora;

TEST(LspDiagConvert, ErrorBecomesSeverity1) {
    Diagnostic d;
    d.level = DiagLevel::Error;
    d.code = "E0142";
    d.message = "boom";
    d.span.start_line = 5;   // 1-based in mora
    d.span.start_col  = 3;
    d.span.end_line   = 5;
    d.span.end_col    = 8;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["severity"], 1);
    EXPECT_EQ(j["code"], "E0142");
    EXPECT_EQ(j["source"], "mora");
    EXPECT_EQ(j["message"], "boom");
    EXPECT_EQ(j["range"]["start"]["line"], 4);       // 0-based
    EXPECT_EQ(j["range"]["start"]["character"], 2);
    EXPECT_EQ(j["range"]["end"]["line"], 4);
    EXPECT_EQ(j["range"]["end"]["character"], 7);
}

TEST(LspDiagConvert, WarningBecomesSeverity2) {
    Diagnostic d;
    d.level = DiagLevel::Warning;
    d.span.start_line = 1;
    d.span.start_col  = 1;
    d.span.end_line   = 1;
    d.span.end_col    = 2;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["severity"], 2);
}

TEST(LspDiagConvert, NoteBecomesSeverity3) {
    Diagnostic d;
    d.level = DiagLevel::Note;
    d.span.start_line = 1;
    d.span.start_col  = 1;
    d.span.end_line   = 1;
    d.span.end_col    = 2;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["severity"], 3);
}

TEST(LspDiagConvert, ZeroSpanCoordinatesClampToZero) {
    Diagnostic d;
    d.level = DiagLevel::Error;
    d.span.start_line = 0;
    d.span.start_col  = 0;
    d.span.end_line   = 0;
    d.span.end_col    = 0;

    auto j = lsp::diagnostic_to_json(d);
    EXPECT_EQ(j["range"]["start"]["line"], 0);
    EXPECT_EQ(j["range"]["start"]["character"], 0);
    EXPECT_EQ(j["range"]["end"]["line"], 0);
    EXPECT_EQ(j["range"]["end"]["character"], 0);
}
