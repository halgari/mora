#include "mora/cli/progress.h"
#include "mora/cli/splash.h"
#include <cstdio>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace mora {

Progress::Progress(bool use_color, bool is_tty)
    : color_(use_color), is_tty_(is_tty), phase_start_(std::chrono::steady_clock::now()) {}

void Progress::print_header(const std::string& version) {
    // ASCII art banner
    std::string banner(MORA_BANNER);
    std::printf("%s\n", TermStyle::cyan(banner, color_).c_str());

    // Version + random splash tagline
    std::string ver = TermStyle::dim("   v" + version, color_);
    std::string splash = TermStyle::dim(std::string("   ") + random_splash(), color_);
    std::printf("%s\n%s\n\n", ver.c_str(), splash.c_str());
}

void Progress::start_phase(const std::string& name) {
    phase_start_ = std::chrono::steady_clock::now();
    if (is_tty_) {
        std::string spinner = TermStyle::cyan("  ●", color_);
        std::string label = TermStyle::dim(name + "...", color_);
        std::printf("\r%s %s", spinner.c_str(), label.c_str());
        std::fflush(stdout);
    }
}

static std::string format_duration(long ms) {
    if (ms < 1) return "<1ms";
    if (ms < 1000) return std::to_string(ms) + "ms";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << (double(ms) / 1000.0) << "s";
    return ss.str();
}

void Progress::finish_phase(const std::string& detail, const std::string& timing) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - phase_start_).count();

    std::string check = TermStyle::green("  ✓", color_);
    std::string desc = detail;
    std::string time_str = TermStyle::dim(format_duration(elapsed), color_);

    if (is_tty_) std::printf("\r\033[K"); // clear line

    // Build dots between description and timing
    size_t desc_len = detail.size() + 4; // "  ✓ " prefix
    size_t time_plain_len = format_duration(elapsed).size();
    size_t total_width = 72;
    size_t dots_len = 0;
    if (desc_len + time_plain_len + 2 < total_width) {
        dots_len = total_width - desc_len - time_plain_len;
    }
    std::string dots;
    for (size_t i = 0; i < dots_len; i++) {
        dots += (i % 2 == 0) ? ' ' : '.';
    }

    std::printf("%s %s %s%s\n", check.c_str(), desc.c_str(),
                TermStyle::dim(dots, color_).c_str(), time_str.c_str());
}

void Progress::print_success(const std::string& message) {
    std::printf("\n  %s %s\n\n",
        TermStyle::green("✓", color_).c_str(),
        TermStyle::bold(TermStyle::green(message, color_), color_).c_str());
}

void Progress::print_failure(const std::string& message) {
    std::printf("\n  %s %s\n\n",
        TermStyle::red("✗", color_).c_str(),
        TermStyle::bold(TermStyle::red(message, color_), color_).c_str());
}

void Progress::print_summary(size_t frozen_rules, size_t frozen_patches,
                             size_t dynamic_rules, size_t conflicts,
                             size_t errors, size_t warnings,
                             const std::string& patch_size) {
    std::printf("\n");

    auto line = [&](const char* label, const std::string& value, const std::string& extra = "") {
        std::string lbl = TermStyle::dim(std::string(label), color_);
        std::string val = TermStyle::bold(value, color_);
        if (extra.empty()) {
            std::printf("    %s %s\n", lbl.c_str(), val.c_str());
        } else {
            std::printf("    %s %s %s\n", lbl.c_str(), val.c_str(),
                       TermStyle::dim(extra, color_).c_str());
        }
    };

    line("Frozen:", std::to_string(frozen_rules) + " rules",
         "→ mora.patch (" + patch_size + ", " + std::to_string(frozen_patches) + " patches)");

    if (dynamic_rules > 0) {
        line("Dynamic:", std::to_string(dynamic_rules) + " rules", "→ mora.rt");
    }

    if (conflicts > 0) {
        line("Conflicts:", TermStyle::yellow(std::to_string(conflicts), color_), "(see mora.log)");
    }

    if (errors > 0) {
        line("Errors:", TermStyle::red(std::to_string(errors), color_));
    }
    if (warnings > 0) {
        line("Warnings:", TermStyle::yellow(std::to_string(warnings), color_));
    }

    std::printf("\n");
}

} // namespace mora
