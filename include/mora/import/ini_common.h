#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace mora {

struct FormRef {
    std::string editor_id;     // "ActorTypeNPC"
    std::string plugin;        // "Skyrim.esm" (empty if editor_id only)
    uint32_t form_id = 0;     // hex value (0 if editor_id only)

    bool is_editor_id() const { return plugin.empty() && form_id == 0; }
    std::string to_mora_symbol() const;  // ":EditorID" or ":Plugin.esp|0xHEX"
};

// Resolves FormIDs to EditorIDs using ESP data (optional, improves output quality)
class FormIdResolver {
public:
    void build_from_editor_ids(const std::unordered_map<std::string, uint32_t>& editor_ids);
    std::string resolve(uint32_t formid) const;
    std::string resolve_ref(const FormRef& ref) const;
    bool has_data() const { return !reverse_.empty(); }

private:
    std::unordered_map<uint32_t, std::string> reverse_;
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

// Shared AST construction helpers for INI parsers
Expr make_var(StringPool& pool, const char* name);
Expr make_sym(StringPool& pool, std::string_view name);
Expr make_int(int64_t value);
Expr make_float(double value);
Clause make_fact(StringPool& pool, std::string_view fact_name,
                 std::vector<Expr> args, bool negated = false);
Clause make_guard(BinaryExpr::Op op, Expr left, Expr right);

// Resolve a FormRef to a display string, using optional resolver for EditorID lookup
std::string resolve_symbol(const FormRef& ref, const FormIdResolver* resolver);

// Sanitize a string into a valid Mora identifier (lowercase, underscores)
std::string sanitize_name(std::string_view s);

// Parse an INI file line-by-line, calling parse_line for each non-empty, non-comment line.
// Returns collected rules from all lines.
template <typename ParseLine>
std::vector<Rule> parse_ini_file_lines(const std::string& path, DiagBag& diags,
                                        const char* diag_code, ParseLine parse_line) {
    std::vector<Rule> all_rules;
    std::ifstream file(path);
    if (!file.is_open()) {
        diags.add(Diagnostic{DiagLevel::Warning, diag_code,
            "Could not open file: " + path, {}});
        return all_rules;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    while (std::getline(stream, line)) {
        line_num++;
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';') continue;
        auto rules = parse_line(trimmed, line_num);
        for (auto& r : rules) all_rules.push_back(std::move(r));
    }
    return all_rules;
}

} // namespace mora
