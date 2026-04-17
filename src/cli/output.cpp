#include "mora/cli/output.h"
#include "mora/cli/splash.h"
#include <fmt/format.h>

namespace mora {

Output::Output(bool use_color, bool is_tty)
    : color_(use_color), is_tty_(is_tty), phase_start_(std::chrono::steady_clock::now()) {}

std::string Output::format_duration(long long ms) {
    if (ms < 1) return "<1ms";
    if (ms < 1000) return fmt::format("{}ms", ms);
    return fmt::format("{:.1f}s", double(ms) / 1000.0);
}

std::string Output::dot_pad(std::string_view left, std::string_view right, size_t width) {
    size_t const left_len = left.size() + 4; // "  ✓ " prefix
    size_t const right_len = right.size();
    size_t dots_len = 0;
    if (left_len + right_len + 2 < width) {
        dots_len = width - left_len - right_len;
    }
    std::string dots;
    dots.reserve(dots_len);
    for (size_t i = 0; i < dots_len; i++) {
        dots += (i % 2 == 0) ? ' ' : '.';
    }
    return dots;
}

void Output::print_header(std::string_view version) const {
    std::string const banner(MORA_BANNER);
    fmt::print("{}\n", TermStyle::cyan(banner, color_));
    fmt::print("{}\n{}\n\n",
        TermStyle::dim(fmt::format("   v{}", version), color_),
        TermStyle::dim(fmt::format("   {}", random_splash()), color_));
}

void Output::phase_start(std::string_view name) {
    phase_start_ = std::chrono::steady_clock::now();
    if (is_tty_) {
        fmt::print("\r{} {}",
            TermStyle::cyan("  \xe2\x97\x8f", color_),
            TermStyle::dim(fmt::format("{}...", name), color_));
        std::fflush(stdout);
    }
}

void Output::phase_done(std::string_view detail) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_).count();

    if (is_tty_) fmt::print("\r\033[K");

    auto time_str = format_duration(elapsed);
    auto dots = dot_pad(detail, time_str);

    fmt::print("{} {} {}{}\n",
        TermStyle::green("  \xe2\x9c\x93", color_),
        detail,
        TermStyle::dim(dots, color_),
        TermStyle::dim(time_str, color_));
}

void Output::success(std::string_view message) const {
    fmt::print("\n  {} {}\n\n",
        TermStyle::green("\xe2\x9c\x93", color_),
        TermStyle::bold(TermStyle::green(std::string(message), color_), color_));
}

void Output::failure(std::string_view message) const {
    fmt::print("\n  {} {}\n\n",
        TermStyle::red("\xe2\x9c\x97", color_),
        TermStyle::bold(TermStyle::red(std::string(message), color_), color_));
}

void Output::table(const std::vector<TableRow>& rows) const {
    fmt::print("\n");
    for (auto& row : rows) {
        auto lbl = TermStyle::dim(row.label, color_);
        auto val = TermStyle::bold(row.value, color_);
        if (row.extra.empty()) {
            fmt::print("    {} {}\n", lbl, val);
        } else {
            fmt::print("    {} {} {}\n", lbl, val, TermStyle::dim(row.extra, color_));
        }
    }
    fmt::print("\n");
}

void Output::section_header(std::string_view title, size_t width) const {
    std::string t = fmt::format(" {} ", title);
    size_t const fill = (width > t.size() + 4) ? width - t.size() - 4 : 0;
    std::string fill_str;
    fill_str.reserve(fill * 3); // UTF-8 ─ is 3 bytes
    for (size_t i = 0; i < fill; ++i) fill_str += "\xe2\x94\x80";
    fmt::print("  {}\n",
        TermStyle::cyan(fmt::format("\xe2\x94\x80\xe2\x94\x80{}{}", t, fill_str), color_));
}

void Output::progress_update(std::string_view text) const {
    if (is_tty_) {
        fmt::print(stderr, "\r  {}", text);
        std::fflush(stderr);
    }
}

void Output::progress_clear() const {
    if (is_tty_) {
        fmt::print(stderr, "\r{:60s}\r", "");
        std::fflush(stderr);
    }
}

void Output::step_done(std::string_view label, std::string_view detail) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_).count();
    auto time_str = format_duration(elapsed);

    std::string left;
    if (detail.empty()) {
        left = std::string(label);
    } else {
        left = fmt::format("{} ({})", label, detail);
    }
    auto dots = dot_pad(left, time_str);

    fmt::print("  {} {} {}{}\n",
        TermStyle::green("\xe2\x9c\x93", color_),
        left,
        TermStyle::dim(dots, color_),
        TermStyle::dim(time_str, color_));

    // Reset phase timer for next step
    phase_start_ = std::chrono::steady_clock::now();
}

} // namespace mora
