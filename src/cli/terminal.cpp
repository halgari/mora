#include "mora/cli/terminal.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

namespace mora {

namespace {
    constexpr const char* kResetSeq  = "\033[0m";
    constexpr const char* kRedSeq    = "\033[31m";
    constexpr const char* kYellowSeq = "\033[33m";
    constexpr const char* kCyanSeq   = "\033[36m";
    constexpr const char* kGreenSeq  = "\033[32m";
    constexpr const char* kBoldSeq   = "\033[1m";
    constexpr const char* kDimSeq    = "\033[2m";

    std::string wrap(std::string_view s, const char* open, bool color) {
        if (!color) return std::string(s);
        return std::string(open) + std::string(s) + kResetSeq;
    }
} // anonymous namespace

std::string TermStyle::red(std::string_view s, bool color) { return wrap(s, kRedSeq, color); }
std::string TermStyle::yellow(std::string_view s, bool color) { return wrap(s, kYellowSeq, color); }
std::string TermStyle::cyan(std::string_view s, bool color) { return wrap(s, kCyanSeq, color); }
std::string TermStyle::green(std::string_view s, bool color) { return wrap(s, kGreenSeq, color); }
std::string TermStyle::bold(std::string_view s, bool color) { return wrap(s, kBoldSeq, color); }
std::string TermStyle::dim(std::string_view s, bool color) { return wrap(s, kDimSeq, color); }

std::string TermStyle::reset(bool color) {
    if (!color) return "";
    return kResetSeq;
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
