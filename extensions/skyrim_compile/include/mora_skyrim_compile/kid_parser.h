#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mora_skyrim_compile {

// A single form reference appearing in a KID .ini file.
//
// KID accepts two encodings for keyword/item identifiers:
//   - EditorID:                "MyKeyword" or "ArmorHeavy"
//   - FormID with plugin:      "0xFFFFFF~Mod.esp"  (ESP/ESM, up to 6 hex digits)
//                              "0xFFF~Mod.esl"     (ESL/light, 3 hex digits)
//
// The parser stores either form verbatim; resolution to a numeric FormID
// happens later (see kid_resolver.cpp) once the ESP datasource has produced
// the EditorID map.
struct KidRef {
    // If `editor_id` is non-empty this is an EditorID reference and
    // `formid` / `mod_file` are unset.
    std::string editor_id;

    // For FormID references, `formid` holds the parsed hex value and
    // `mod_file` the plugin filename (with extension, case as written).
    uint32_t    formid = 0;
    std::string mod_file;

    bool is_editor_id() const { return !editor_id.empty(); }
};

// One clause within a KID FilterStrings field. KID filters are an
// AND-of-ORs: values joined by "," are OR'd; values joined by "+" are
// AND'd. We store each AND-group as a vector of alternatives (the ORs);
// the full filter is the vector of those groups.
struct KidFilterGroup {
    std::vector<KidRef> values;
};

// One parsed KID line. Mirrors the grammar:
//     TargetKeyword | ItemType | FilterStrings | Traits | Chance
struct KidLine {
    KidRef                       target;        // field 0
    std::string                  item_type;     // field 1, lowercased+snake
    std::vector<KidFilterGroup>  filter;        // field 2, parsed
    std::vector<std::string>     traits;        // field 3, raw tokens (E, -E, HEAVY, ...)
    double                       chance = 100.0; // field 4
    int                          source_line = 0;
    std::string                  raw;           // original line, for diagnostics
};

// A parsed KID file with any diagnostics the parser produced.
struct KidDiag {
    int    line = 0;
    std::string message;
};

struct KidFile {
    std::filesystem::path     path;
    std::vector<KidLine>      lines;
    std::vector<KidDiag>      diags;
};

// Parse a KID .ini file from disk. Malformed lines produce entries in
// `diags` and are skipped. Comment lines (";" or "#") and section
// headers ("[...]") are silently ignored. Returns an empty file with a
// single diag if the file can't be opened.
KidFile parse_kid_file(const std::filesystem::path& path);

// Parse a single KID line. Exposed for unit tests; handles optional
// "Keyword =" prefix and returns std::nullopt equivalence via an empty
// KidLine if `raw` is a comment / header / empty (the caller sees
// `item_type.empty()` and skips). `line_num` is propagated into
// `KidLine::source_line` and any diagnostics.
bool parse_kid_line(std::string_view raw, int line_num,
                    KidLine& out, std::vector<KidDiag>& diags);

} // namespace mora_skyrim_compile
