#include "mora/import/ini_facts.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mora {

// Map KID item type string to a relation name.
// Returns empty string for unknown types.
static std::string kid_item_type_to_relation(const std::string& item_type) {
    std::string lower = item_type;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "weapon" || lower == "armor" || lower == "ammo" ||
        lower == "potion" || lower == "book") {
        return lower;
    }
    if (lower == "spell")        return "spell";
    if (lower == "race")         return "race";
    if (lower == "misc item")    return "misc_item";
    if (lower == "magic effect") return "magic_effect";
    if (lower == "ingredient")   return "ingredient";
    if (lower == "activator")    return "activator";
    if (lower == "flora")        return "flora";
    if (lower == "scroll")       return "scroll";
    if (lower == "soul gem")     return "soul_gem";
    if (lower == "location")     return "location";
    if (lower == "key")          return "key";
    if (lower == "furniture")    return "furniture";
    if (lower == "enchantment")  return "enchantment";
    return {};
}

// Resolve a FilterEntry to a Value. If the editor_id_map has a FormID
// mapping, return make_formid. Otherwise fall back to make_string.
static Value resolve_filter_entry(const FilterEntry& entry,
                                   StringPool& pool,
                                   const EditorIdMap& editor_id_map) {
    if (entry.ref.is_editor_id()) {
        auto it = editor_id_map.find(entry.ref.editor_id);
        if (it != editor_id_map.end()) {
            return Value::make_formid(it->second);
        }
        // Unresolved — keep as string (won't match FormID facts but preserves data)
        return Value::make_string(pool.intern(entry.ref.editor_id));
    }
    // Form reference with plugin: resolve from hex FormID
    // The form_id in the ref is the local ID; for matching against the FactDB
    // we'd need the resolved global ID. For now, store as FormID if we have it.
    if (entry.ref.form_id != 0) {
        return Value::make_formid(entry.ref.form_id);
    }
    return Value::make_string(pool.intern(entry.ref.to_mora_symbol()));
}

// Emit filter/exclude facts from a list of FilterEntry values.
static void emit_filter_facts(const std::vector<FilterEntry>& entries,
                               uint32_t rule_id,
                               const std::string& filter_kind,
                               StringId include_rel,
                               StringId exclude_rel,
                               FactDB& db, StringPool& pool,
                               const EditorIdMap& editor_id_map) {
    std::vector<Value> includes;
    std::vector<Value> excludes;

    for (const auto& entry : entries) {
        auto val = resolve_filter_entry(entry, pool, editor_id_map);

        if (entry.mode == FilterEntry::Mode::Exclude) {
            excludes.push_back(std::move(val));
        } else {
            includes.push_back(std::move(val));
        }
    }

    auto kind_val = Value::make_string(pool.intern(filter_kind));

    if (!includes.empty()) {
        db.add_fact(include_rel, {
            Value::make_int(static_cast<int64_t>(rule_id)),
            kind_val,
            Value::make_list(std::move(includes))
        });
    }
    if (!excludes.empty()) {
        db.add_fact(exclude_rel, {
            Value::make_int(static_cast<int64_t>(rule_id)),
            kind_val,
            Value::make_list(std::move(excludes))
        });
    }
}

static size_t emit_spid_line(const std::string& line,
                              const std::string& filename,
                              int line_num,
                              FactDB& db, StringPool& pool,
                              DiagBag& diags,
                              uint32_t& next_rule_id,
                              const EditorIdMap& editor_id_map) {
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
        return 0;
    }

    auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string_view::npos) return 0;

    auto type_str = std::string(trim(trimmed.substr(0, eq_pos)));
    auto value_str = std::string(trim(trimmed.substr(eq_pos + 1)));

    std::string type_lower = type_str;
    std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Validate dist type
    if (type_lower != "keyword" && type_lower != "spell" &&
        type_lower != "perk" && type_lower != "item" &&
        type_lower != "faction") {
        return 0;
    }

    auto fields = split_pipes(value_str);
    if (fields.empty()) return 0;

    uint32_t rule_id = next_rule_id++;

    // Field 0: target form — resolve to FormID if possible
    auto target_ref = parse_form_ref(fields[0]);
    Value target_val;
    if (target_ref.is_editor_id()) {
        auto it = editor_id_map.find(target_ref.editor_id);
        if (it != editor_id_map.end()) {
            target_val = Value::make_formid(it->second);
        } else {
            target_val = Value::make_string(pool.intern(target_ref.editor_id));
        }
    } else if (target_ref.form_id != 0) {
        target_val = Value::make_formid(target_ref.form_id);
    } else {
        target_val = Value::make_string(pool.intern(target_ref.to_mora_symbol()));
    }

    // Emit spid_dist(RuleID, DistType, Target)
    auto spid_dist_rel = pool.intern("spid_dist");
    db.add_fact(spid_dist_rel, {
        Value::make_int(static_cast<int64_t>(rule_id)),
        Value::make_string(pool.intern(type_lower)),
        target_val
    });

    // Field 1: string filters (keyword/editor_id)
    if (fields.size() > 1 && !fields[1].empty()) {
        auto entries = parse_filter_entries(fields[1]);
        if (!entries.empty()) {
            emit_filter_facts(entries, rule_id, "keyword",
                              pool.intern("spid_filter"),
                              pool.intern("spid_exclude"),
                              db, pool, editor_id_map);
        }
    }

    // Field 2: form filters
    if (fields.size() > 2 && !fields[2].empty()) {
        auto entries = parse_filter_entries(fields[2]);
        if (!entries.empty()) {
            emit_filter_facts(entries, rule_id, "form",
                              pool.intern("spid_filter"),
                              pool.intern("spid_exclude"),
                              db, pool, editor_id_map);
        }
    }

    // Field 3: level range
    if (fields.size() > 3 && !fields[3].empty()) {
        auto range = parse_level_range(fields[3]);
        if (range.has_min || range.has_max) {
            auto spid_level_rel = pool.intern("spid_level");
            db.add_fact(spid_level_rel, {
                Value::make_int(static_cast<int64_t>(rule_id)),
                Value::make_int(static_cast<int64_t>(range.has_min ? range.min : 0)),
                Value::make_int(static_cast<int64_t>(range.has_max ? range.max : 0))
            });
        }
    }

    // Fields 4-6: traits, count, chance — not emitted
    return 1;
}

size_t emit_spid_facts(const std::string& path, FactDB& db,
                        StringPool& pool, DiagBag& diags,
                        uint32_t& next_rule_id,
                        const EditorIdMap& editor_id_map) {
    std::ifstream file(path);
    if (!file.is_open()) {
        diags.error("E_SPID_FILE", "Cannot open file: " + path, {}, "");
        return 0;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    size_t count = 0;

    while (std::getline(stream, line)) {
        ++line_num;
        count += emit_spid_line(line, path, line_num, db, pool, diags,
                                next_rule_id, editor_id_map);
    }
    return count;
}

static size_t emit_kid_line(const std::string& line,
                             const std::string& filename,
                             int line_num,
                             FactDB& db, StringPool& pool,
                             DiagBag& diags,
                             uint32_t& next_rule_id,
                             const EditorIdMap& editor_id_map) {
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
        return 0;
    }

    auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string_view::npos) return 0;

    auto type_str = std::string(trim(trimmed.substr(0, eq_pos)));
    auto value_str = std::string(trim(trimmed.substr(eq_pos + 1)));

    // KID only has Keyword entries
    std::string type_lower = type_str;
    std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (type_lower != "keyword") {
        return 0;
    }

    auto fields = split_pipes(value_str);
    if (fields.empty()) return 0;

    // Field 0: keyword to assign — resolve to FormID if possible
    auto keyword_ref = parse_form_ref(fields[0]);
    Value keyword_val;
    if (keyword_ref.is_editor_id()) {
        auto it = editor_id_map.find(keyword_ref.editor_id);
        if (it != editor_id_map.end()) {
            keyword_val = Value::make_formid(it->second);
        } else {
            keyword_val = Value::make_string(pool.intern(keyword_ref.editor_id));
        }
    } else if (keyword_ref.form_id != 0) {
        keyword_val = Value::make_formid(keyword_ref.form_id);
    } else {
        keyword_val = Value::make_string(pool.intern(keyword_ref.to_mora_symbol()));
    }

    // Field 1: item type
    std::string item_type;
    if (fields.size() > 1) {
        item_type = fields[1];
    }
    std::string relation = kid_item_type_to_relation(item_type);
    if (relation.empty() && !item_type.empty()) {
        diags.warning("W_KID_UNKNOWN_TYPE",
                       "Unknown KID item type '" + item_type + "', skipping",
                       {filename, static_cast<uint32_t>(line_num), 0,
                        static_cast<uint32_t>(line_num), 0},
                       "");
        return 0;
    }

    uint32_t rule_id = next_rule_id++;

    // Emit kid_dist(RuleID, TargetKeyword, ItemType)
    auto kid_dist_rel = pool.intern("kid_dist");
    db.add_fact(kid_dist_rel, {
        Value::make_int(static_cast<int64_t>(rule_id)),
        keyword_val,
        Value::make_string(pool.intern(relation))
    });

    // Field 2: filters
    if (fields.size() > 2 && !fields[2].empty()) {
        auto entries = parse_filter_entries(fields[2]);
        if (!entries.empty()) {
            emit_filter_facts(entries, rule_id, "keyword",
                              pool.intern("kid_filter"),
                              pool.intern("kid_exclude"),
                              db, pool, editor_id_map);
        }
    }

    // Fields 3-4: traits, chance — not emitted
    return 1;
}

size_t emit_kid_facts(const std::string& path, FactDB& db,
                       StringPool& pool, DiagBag& diags,
                       uint32_t& next_rule_id,
                       const EditorIdMap& editor_id_map) {
    std::ifstream file(path);
    if (!file.is_open()) {
        diags.error("E_KID_FILE", "Cannot open file: " + path, {}, "");
        return 0;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    size_t count = 0;

    while (std::getline(stream, line)) {
        ++line_num;
        count += emit_kid_line(line, path, line_num, db, pool, diags,
                               next_rule_id, editor_id_map);
    }
    return count;
}

void configure_ini_relations(FactDB& db, StringPool& pool) {
    db.configure_relation(pool.intern("spid_dist"), 3, {0, 1});
    db.configure_relation(pool.intern("spid_filter"), 3, {0, 1});
    db.configure_relation(pool.intern("spid_exclude"), 3, {0, 1});
    db.configure_relation(pool.intern("spid_level"), 3, {0});
    db.configure_relation(pool.intern("kid_dist"), 3, {0, 2});
    db.configure_relation(pool.intern("kid_filter"), 3, {0, 1});
    db.configure_relation(pool.intern("kid_exclude"), 3, {0, 1});
}

} // namespace mora
