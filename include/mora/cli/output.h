#pragma once
#include "mora/cli/terminal.h"
#include <fmt/format.h>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
#include <cstdio>

namespace mora {

// Structured row for table output
struct TableRow {
    std::string label;
    std::string value;
    std::string extra;  // optional dim suffix
};

class Output {
public:
    explicit Output(bool use_color = true, bool is_tty = true);

    // Banner + version + splash tagline
    void print_header(std::string_view version) const;

    // Phase tracking with spinner + dot-padded timing
    void phase_start(std::string_view name);
    void phase_done(std::string_view detail);

    // Styled result lines
    void success(std::string_view message) const;
    void failure(std::string_view message) const;

    // Auto-aligned table with dim labels, bold values
    void table(const std::vector<TableRow>& rows) const;

    // Box-drawing section header: ── title ──────
    void section_header(std::string_view title, size_t width = 54) const;

    // Inline stderr progress (carriage-return overwrite)
    void progress_update(std::string_view text) const;
    void progress_clear() const;

    // Sub-step with dot-padded timing (for codegen breakdown etc.)
    void step_done(std::string_view label, std::string_view detail = {});

    // Raw styled text helpers (delegate to TermStyle)
    bool color() const { return color_; }
    bool is_tty() const { return is_tty_; }

private:
    bool color_;
    bool is_tty_;
    std::chrono::steady_clock::time_point phase_start_;

    static std::string format_duration(long long ms);
    static std::string dot_pad(std::string_view left, std::string_view right, size_t width = 72) ;
};

} // namespace mora
