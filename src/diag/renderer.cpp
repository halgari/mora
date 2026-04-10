#include "mora/diag/renderer.h"
#include <sstream>
#include <string>

namespace mora {

namespace {

// Produce an underline of `len` dashes starting at column `start_col` (1-based)
// under a source line. Returns a string with spaces up to start_col-1 then dashes.
std::string make_underline(uint32_t start_col, uint32_t end_col) {
    if (start_col == 0) return "";
    std::string result;
    // start_col is 1-based; indent by (start_col - 1) spaces
    result.append(start_col > 0 ? start_col - 1 : 0, ' ');
    uint32_t len = (end_col > start_col) ? (end_col - start_col) : 1;
    result.append(len, '-');
    return result;
}

} // anonymous namespace

std::string DiagRenderer::level_str(DiagLevel level) const {
    switch (level) {
        case DiagLevel::Error:   return "error";
        case DiagLevel::Warning: return "warning";
        case DiagLevel::Note:    return "note";
    }
    return "error";
}

std::string DiagRenderer::render(const Diagnostic& diag) const {
    std::ostringstream out;

    // Header: error[E012]: type mismatch
    std::string lvl = level_str(diag.level);
    std::string header;
    if (!diag.code.empty()) {
        header = lvl + "[" + diag.code + "]: " + diag.message;
    } else {
        header = lvl + ": " + diag.message;
    }

    // Apply color to the level word
    std::string colored_header;
    if (color_) {
        std::string styled_lvl;
        if (diag.level == DiagLevel::Error) {
            styled_lvl = TermStyle::bold(TermStyle::red(lvl, color_), color_);
        } else if (diag.level == DiagLevel::Warning) {
            styled_lvl = TermStyle::bold(TermStyle::yellow(lvl, color_), color_);
        } else {
            styled_lvl = TermStyle::bold(TermStyle::cyan(lvl, color_), color_);
        }
        if (!diag.code.empty()) {
            colored_header = styled_lvl + TermStyle::bold("[" + diag.code + "]: " + diag.message, color_);
        } else {
            colored_header = styled_lvl + TermStyle::bold(": " + diag.message, color_);
        }
    } else {
        colored_header = header;
    }

    out << "  " << colored_header << "\n";

    // Location line: ┌─ file:line:col
    if (!diag.span.file.empty()) {
        std::string loc = diag.span.file + ":"
                        + std::to_string(diag.span.start_line) + ":"
                        + std::to_string(diag.span.start_col);
        out << "    " << TermStyle::dim("\u250C\u2500", color_)
            << " " << TermStyle::cyan(loc, color_) << "\n";
        out << "    " << TermStyle::dim("\u2502", color_) << "\n";
    }

    // Source line with line number
    if (!diag.source_line.empty()) {
        std::string line_num = std::to_string(diag.span.start_line);
        out << "  " << TermStyle::dim(line_num, color_)
            << TermStyle::dim("\u2502", color_)
            << diag.source_line << "\n";

        // Underline
        std::string ul = make_underline(diag.span.start_col, diag.span.end_col);
        if (!ul.empty()) {
            std::string colored_ul;
            if (diag.level == DiagLevel::Error) {
                colored_ul = TermStyle::red(ul, color_);
            } else if (diag.level == DiagLevel::Warning) {
                colored_ul = TermStyle::yellow(ul, color_);
            } else {
                colored_ul = TermStyle::cyan(ul, color_);
            }
            out << "    " << TermStyle::dim("\u2502", color_)
                << colored_ul << "\n";
        }
        out << "    " << TermStyle::dim("\u2502", color_) << "\n";
    }

    // Notes
    for (const auto& note : diag.notes) {
        out << "    " << TermStyle::dim("=", color_) << " " << note << "\n";
    }

    return out.str();
}

std::string DiagRenderer::render_all(const DiagBag& bag) const {
    std::ostringstream out;
    for (const auto& diag : bag.all()) {
        out << render(diag);
    }

    // Summary line
    size_t errs = bag.error_count();
    size_t warns = bag.warning_count();
    if (errs > 0 || warns > 0) {
        out << "\n";
        if (errs > 0) {
            std::string summary = std::to_string(errs) + " error" + (errs != 1 ? "s" : "");
            out << TermStyle::bold(TermStyle::red(summary, color_), color_);
        }
        if (errs > 0 && warns > 0) {
            out << ", ";
        }
        if (warns > 0) {
            std::string summary = std::to_string(warns) + " warning" + (warns != 1 ? "s" : "");
            out << TermStyle::bold(TermStyle::yellow(summary, color_), color_);
        }
        out << "\n";
    }

    return out.str();
}

} // namespace mora
