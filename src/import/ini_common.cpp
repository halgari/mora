#include "mora/import/ini_common.h"
#include <algorithm>
#include <charconv>
#include <sstream>

namespace mora {

std::string_view trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string FormRef::to_mora_symbol() const {
    if (is_editor_id()) {
        return ":" + editor_id;
    }
    // Format form_id as 0x padded to 8 hex digits
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", form_id);
    return ":" + plugin + "|" + buf;
}

FormRef parse_form_ref(std::string_view text) {
    text = trim(text);
    FormRef ref;

    auto tilde = text.find('~');
    if (tilde != std::string_view::npos) {
        // "0xHEX~Plugin.esp"
        std::string_view hex_part = trim(text.substr(0, tilde));
        std::string_view plugin_part = trim(text.substr(tilde + 1));
        ref.plugin = std::string(plugin_part);

        // Strip leading "0x" or "0X"
        if (hex_part.size() >= 2 &&
            hex_part[0] == '0' && (hex_part[1] == 'x' || hex_part[1] == 'X')) {
            hex_part = hex_part.substr(2);
        }
        uint32_t val = 0;
        std::from_chars(hex_part.data(), hex_part.data() + hex_part.size(), val, 16);
        ref.form_id = val;
    } else if (text.size() >= 2 &&
               text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        // "0xHEX" without plugin — base game, no plugin string
        std::string_view hex_part = text.substr(2);
        uint32_t val = 0;
        std::from_chars(hex_part.data(), hex_part.data() + hex_part.size(), val, 16);
        ref.form_id = val;
        // plugin stays empty; form_id != 0 so is_editor_id() returns false
    } else {
        // Plain EditorID
        ref.editor_id = std::string(text);
    }
    return ref;
}

std::vector<FilterEntry> parse_filter_entries(std::string_view text) {
    std::vector<FilterEntry> entries;
    while (!text.empty()) {
        auto comma = text.find(',');
        std::string_view token = trim(text.substr(0, comma));
        if (comma == std::string_view::npos)
            text = {};
        else
            text = text.substr(comma + 1);

        if (token.empty()) continue;

        FilterEntry entry;
        if (token[0] == '+') {
            entry.mode = FilterEntry::Mode::And;
            token = token.substr(1);
        } else if (token[0] == '-') {
            entry.mode = FilterEntry::Mode::Exclude;
            token = token.substr(1);
        } else if (token[0] == '*') {
            entry.mode = FilterEntry::Mode::Wildcard;
            entry.pattern = std::string(token);
            entries.push_back(std::move(entry));
            continue;
        } else {
            entry.mode = FilterEntry::Mode::Include;
        }

        entry.ref = parse_form_ref(token);
        entries.push_back(std::move(entry));
    }
    return entries;
}

LevelRange parse_level_range(std::string_view text) {
    LevelRange range;
    text = trim(text);
    auto slash = text.find('/');
    if (slash == std::string_view::npos) {
        // No slash — treat entire text as min
        std::string_view part = trim(text);
        if (!part.empty()) {
            int val = 0;
            std::from_chars(part.data(), part.data() + part.size(), val);
            range.min = val;
            range.has_min = true;
        }
        return range;
    }

    std::string_view min_part = trim(text.substr(0, slash));
    std::string_view max_part = trim(text.substr(slash + 1));

    if (!min_part.empty()) {
        int val = 0;
        std::from_chars(min_part.data(), min_part.data() + min_part.size(), val);
        range.min = val;
        range.has_min = true;
    }
    if (!max_part.empty()) {
        int val = 0;
        std::from_chars(max_part.data(), max_part.data() + max_part.size(), val);
        range.max = val;
        range.has_max = true;
    }
    return range;
}

TraitFilter parse_traits(std::string_view text) {
    TraitFilter tf;
    text = trim(text);
    // Split by '/'
    while (!text.empty()) {
        auto slash = text.find('/');
        std::string_view token = trim(text.substr(0, slash));
        if (slash == std::string_view::npos)
            text = {};
        else
            text = text.substr(slash + 1);

        if (token.empty()) continue;

        bool negate = false;
        if (token[0] == '-') {
            negate = true;
            token = token.substr(1);
        }

        if (token == "M") {
            tf.is_male = !negate;
        } else if (token == "F") {
            tf.is_male = negate; // F means not-male = false; -F means male = true
        } else if (token == "U") {
            if (negate)
                tf.not_unique = true;
            else
                tf.is_unique = true;
        }
    }
    return tf;
}

std::vector<std::string> split_pipes(std::string_view line) {
    std::vector<std::string> parts;
    while (true) {
        auto pipe = line.find('|');
        std::string_view part = trim(line.substr(0, pipe));

        if (part == "NONE" || part.empty())
            parts.emplace_back();
        else
            parts.emplace_back(part);

        if (pipe == std::string_view::npos) break;
        line = line.substr(pipe + 1);
    }
    return parts;
}

} // namespace mora
