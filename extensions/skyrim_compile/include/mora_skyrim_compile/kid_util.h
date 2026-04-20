#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace mora_skyrim_compile {

inline std::string to_lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

} // namespace mora_skyrim_compile
