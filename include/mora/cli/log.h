#pragma once
#include "mora/cli/terminal.h"
#include <fmt/format.h>
#include <fmt/color.h>
#include <cstdio>

namespace mora::log {

namespace detail {
    inline bool verbose_enabled = false;
}

inline void set_verbose(bool v) { detail::verbose_enabled = v; }
inline bool verbose() { return detail::verbose_enabled; }

// info: stdout, plain text
template <typename... Args>
void info(fmt::format_string<Args...> fmtstr, Args&&... args) {
    fmt::print(stdout, fmtstr, std::forward<Args>(args)...);
}

// warn: stderr, yellow "warning: " prefix
template <typename... Args>
void warn(fmt::format_string<Args...> fmtstr, Args&&... args) {
    bool c = color_enabled();
    if (c) fmt::print(stderr, "\033[33m");
    fmt::print(stderr, "warning: ");
    if (c) fmt::print(stderr, "\033[0m");
    fmt::print(stderr, fmtstr, std::forward<Args>(args)...);
}

// error: stderr, red "error: " prefix
template <typename... Args>
void error(fmt::format_string<Args...> fmtstr, Args&&... args) {
    bool c = color_enabled();
    if (c) fmt::print(stderr, "\033[31m");
    fmt::print(stderr, "error: ");
    if (c) fmt::print(stderr, "\033[0m");
    fmt::print(stderr, fmtstr, std::forward<Args>(args)...);
}

// debug: stderr, only when verbose
template <typename... Args>
void debug(fmt::format_string<Args...> fmtstr, Args&&... args) {
    if (!detail::verbose_enabled) return;
    bool c = color_enabled();
    if (c) fmt::print(stderr, "\033[2m");
    fmt::print(stderr, fmtstr, std::forward<Args>(args)...);
    if (c) fmt::print(stderr, "\033[0m");
}

} // namespace mora::log
