#pragma once
#include <string>
#include <string_view>

namespace mora {

struct TermStyle {
    static std::string red(std::string_view s, bool color);
    static std::string yellow(std::string_view s, bool color);
    static std::string cyan(std::string_view s, bool color);
    static std::string green(std::string_view s, bool color);
    static std::string bold(std::string_view s, bool color);
    static std::string dim(std::string_view s, bool color);
    static std::string reset(bool color);
};

bool stdout_is_tty();
bool color_enabled(); // checks NO_COLOR env, TTY status

} // namespace mora
