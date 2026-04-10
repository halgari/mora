#include "mora/cli/terminal.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace mora {

namespace {
    constexpr const char* RESET_SEQ  = "\033[0m";
    constexpr const char* RED_SEQ    = "\033[31m";
    constexpr const char* YELLOW_SEQ = "\033[33m";
    constexpr const char* CYAN_SEQ   = "\033[36m";
    constexpr const char* GREEN_SEQ  = "\033[32m";
    constexpr const char* BOLD_SEQ   = "\033[1m";
    constexpr const char* DIM_SEQ    = "\033[2m";

    std::string wrap(const std::string& s, const char* open, bool color) {
        if (!color) return s;
        return std::string(open) + s + RESET_SEQ;
    }
} // anonymous namespace

std::string TermStyle::red(const std::string& s, bool color) {
    return wrap(s, RED_SEQ, color);
}

std::string TermStyle::yellow(const std::string& s, bool color) {
    return wrap(s, YELLOW_SEQ, color);
}

std::string TermStyle::cyan(const std::string& s, bool color) {
    return wrap(s, CYAN_SEQ, color);
}

std::string TermStyle::green(const std::string& s, bool color) {
    return wrap(s, GREEN_SEQ, color);
}

std::string TermStyle::bold(const std::string& s, bool color) {
    return wrap(s, BOLD_SEQ, color);
}

std::string TermStyle::dim(const std::string& s, bool color) {
    return wrap(s, DIM_SEQ, color);
}

std::string TermStyle::reset(bool color) {
    if (!color) return "";
    return RESET_SEQ;
}

bool stdout_is_tty() {
    return isatty(fileno(stdout)) != 0;
}

bool color_enabled() {
    const char* no_color = std::getenv("NO_COLOR");
    if (no_color != nullptr && no_color[0] != '\0') {
        return false;
    }
    return stdout_is_tty();
}

} // namespace mora
