#pragma once
#include <string>

namespace mora {

struct TermStyle {
    static std::string red(const std::string& s, bool color);
    static std::string yellow(const std::string& s, bool color);
    static std::string cyan(const std::string& s, bool color);
    static std::string green(const std::string& s, bool color);
    static std::string bold(const std::string& s, bool color);
    static std::string dim(const std::string& s, bool color);
    static std::string reset(bool color);
};

bool stdout_is_tty();
bool color_enabled(); // checks NO_COLOR env, TTY status

} // namespace mora
