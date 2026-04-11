#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>

namespace mora {

struct FormRef {
    std::string editor_id;     // "ActorTypeNPC"
    std::string plugin;        // "Skyrim.esm" (empty if editor_id only)
    uint32_t form_id = 0;     // hex value (0 if editor_id only)

    bool is_editor_id() const { return plugin.empty() && form_id == 0; }
    std::string to_mora_symbol() const;  // ":EditorID" or ":Plugin.esp|0xHEX"
};

struct FilterEntry {
    enum class Mode { Include, And, Exclude, Wildcard };
    Mode mode = Mode::Include;
    FormRef ref;
    std::string pattern;       // for wildcard matching
};

struct LevelRange {
    int min = 0, max = 0;
    bool has_min = false, has_max = false;
};

struct TraitFilter {
    std::optional<bool> is_male;  // true=M, false=F, nullopt=any
    bool is_unique = false;
    bool not_unique = false;
};

// Parse "0x12345~Plugin.esp" or "EditorID"
FormRef parse_form_ref(std::string_view text);

// Parse "ActorTypeNPC,+Vampire,-Nazeem"
std::vector<FilterEntry> parse_filter_entries(std::string_view text);

// Parse "25/50" or "25/" or "/50"
LevelRange parse_level_range(std::string_view text);

// Parse "M/U" or "F/-U"
TraitFilter parse_traits(std::string_view text);

// Split by pipe, trim each part
std::vector<std::string> split_pipes(std::string_view line);

std::string_view trim(std::string_view s);

} // namespace mora
