#include "mora/lsp/diagnostics_convert.h"

namespace mora::lsp {

namespace {

inline int to_zero_based(uint32_t one_based) {
    return one_based == 0 ? 0 : static_cast<int>(one_based - 1);
}

int severity(mora::DiagLevel level) {
    switch (level) {
        case mora::DiagLevel::Error:   return 1;  // LSP DiagnosticSeverity.Error
        case mora::DiagLevel::Warning: return 2;  // .Warning
        case mora::DiagLevel::Note:    return 3;  // .Information
    }
    return 1;
}

} // namespace

nlohmann::json diagnostic_to_json(const mora::Diagnostic& d) {
    // mora::SourceSpan lines/cols are 1-based; LSP wants 0-based.
    return {
        {"range", {
            {"start", {{"line", to_zero_based(d.span.start_line)}, {"character", to_zero_based(d.span.start_col)}}},
            {"end",   {{"line", to_zero_based(d.span.end_line)},   {"character", to_zero_based(d.span.end_col)}}},
        }},
        {"severity", severity(d.level)},
        {"code",     d.code},
        {"source",   "mora"},
        {"message",  d.message},
    };
}

} // namespace mora::lsp
