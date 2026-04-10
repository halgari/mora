#include "mora/cli/progress.h"
#include <cstdio>
#include <chrono>

namespace mora {

Progress::Progress(bool use_color, bool is_tty)
    : color_(use_color), is_tty_(is_tty), phase_start_(std::chrono::steady_clock::now()) {}

void Progress::print_header(const std::string& version) {
    std::string header = "mora v" + version;
    std::string styled = TermStyle::bold(header, color_);
    std::printf("\n  %s\n\n", styled.c_str());
}

void Progress::start_phase(const std::string& name) {
    phase_start_ = std::chrono::steady_clock::now();
    if (is_tty_) {
        std::printf("\r%s", name.c_str());
        std::fflush(stdout);
    }
}

void Progress::finish_phase(const std::string& detail, const std::string& timing) {
    std::string detail_styled = detail;
    std::string timing_styled = TermStyle::dim(timing, color_);

    // Calculate spacing for right alignment of timing (assuming standard 80 char width)
    size_t detail_len = detail.length();
    size_t timing_len = timing.length();
    size_t target_width = 80;

    if (detail_len + timing_len >= target_width) {
        std::printf("%s %s\n", detail_styled.c_str(), timing_styled.c_str());
    } else {
        size_t padding = target_width - detail_len - timing_len;
        std::printf("%s%*s%s\n", detail_styled.c_str(), (int)padding, "", timing_styled.c_str());
    }
}

void Progress::print_success(const std::string& message) {
    std::string check = "✓";
    std::string styled_check = TermStyle::green(check, color_);
    std::string styled_msg = TermStyle::green(message, color_);
    std::printf("  %s %s\n", styled_check.c_str(), styled_msg.c_str());
}

void Progress::print_failure(const std::string& message) {
    std::string x = "✗";
    std::string styled_x = TermStyle::red(x, color_);
    std::string styled_msg = TermStyle::red(message, color_);
    std::printf("  %s %s\n", styled_x.c_str(), styled_msg.c_str());
}

void Progress::print_summary(size_t frozen_rules, size_t frozen_patches,
                             size_t dynamic_rules, size_t conflicts,
                             size_t errors, size_t warnings,
                             const std::string& spatch_size) {
    std::printf("\n");
    std::printf("  %-30s %zu\n", "Frozen rules:", frozen_rules);
    std::printf("  %-30s %zu\n", "Frozen patches:", frozen_patches);
    std::printf("  %-30s %zu\n", "Dynamic rules:", dynamic_rules);
    std::printf("  %-30s %zu\n", "Conflicts:", conflicts);
    std::printf("  %-30s %zu\n", "Errors:", errors);
    std::printf("  %-30s %zu\n", "Warnings:", warnings);
    std::printf("  %-30s %s\n", "Spatch size:", spatch_size.c_str());
    std::printf("\n");
}

} // namespace mora
