#pragma once
#include <cctype>
#include <string>
#include <string_view>

namespace mora {

// ASCII-only lowercase conversion. Matches the behavior used
// throughout the ESP / plugin layer (Skyrim filenames and editor
// IDs are all ASCII and are compared case-insensitively at runtime,
// so this is the right primitive — not locale-aware, not UTF-8).
//
// Takes by value so callers can move-return an owned copy without
// an extra allocation: `auto lo = to_lower(std::move(s))`.
inline std::string to_lower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline std::string to_lower(std::string_view s) {
    return to_lower(std::string(s));
}

// Case-insensitive equality (ASCII). Cheaper than lowercasing both
// sides for one-shot comparisons.
inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        auto ca = static_cast<unsigned char>(a[i]);
        auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

} // namespace mora
