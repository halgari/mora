#include "mora/import/kid_parser.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mora {

KidParser::KidParser(StringPool& pool, DiagBag& diags,
                     const FormIdResolver* resolver)
    : pool_(pool), diags_(diags), resolver_(resolver) {}

std::string KidParser::resolve_symbol(const FormRef& ref) const {
    if (ref.is_editor_id()) return ref.editor_id;
    if (resolver_ && resolver_->has_data()) {
        auto edid = resolver_->resolve_ref(ref);
        if (!edid.empty()) return edid;
    }
    if (!ref.plugin.empty()) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", ref.form_id);
        return ref.plugin + "|" + buf;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", ref.form_id);
    return std::string(buf);
}

Expr KidParser::make_var(const char* name) {
    Expr e;
    e.data = VariableExpr{pool_.intern(name), {}, {}};
    return e;
}

Expr KidParser::make_sym(const std::string& name) {
    Expr e;
    e.data = SymbolExpr{pool_.intern(name), {}, {}};
    return e;
}

Clause KidParser::make_fact(const std::string& fact_name,
                             std::vector<Expr> args, bool negated) {
    FactPattern fp;
    fp.name = pool_.intern(fact_name);
    fp.args = std::move(args);
    fp.negated = negated;
    Clause c;
    c.data = std::move(fp);
    return c;
}

// Map KID item type string to a relation name.
// Returns empty string for unknown types.
static std::string item_type_to_relation(const std::string& item_type) {
    std::string lower = item_type;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // All supported types map to their lowercase name
    if (lower == "weapon" || lower == "armor" || lower == "ammo" ||
        lower == "potion" || lower == "book") {
        return lower;
    }
    return {};
}

void KidParser::add_item_filters(const std::string& field,
                                  const std::string& item_var,
                                  std::vector<Clause>& body) {
    if (field.empty()) return;
    auto entries = parse_filter_entries(field);
    for (const auto& entry : entries) {
        bool negated = (entry.mode == FilterEntry::Mode::Exclude);
        const auto& ref = entry.ref;

        std::vector<Expr> args;
        args.push_back(make_var(item_var.c_str()));

        if (ref.is_editor_id()) {
            args.push_back(make_sym(ref.editor_id));
            body.push_back(make_fact("has_keyword", std::move(args), negated));
        } else {
            // Form reference with plugin: use to_mora_symbol() stripping leading ':'
            std::string sym = ref.to_mora_symbol();
            if (!sym.empty() && sym[0] == ':') sym = sym.substr(1);
            args.push_back(make_sym(sym));
            body.push_back(make_fact("has_form", std::move(args), negated));
        }
    }
}

std::vector<Rule> KidParser::parse_line(const std::string& line,
                                         const std::string& filename,
                                         int line_num) {
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
        return {};
    }

    // Parse "Keyword = ..."
    auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string_view::npos) {
        return {};
    }

    auto type_str = std::string(trim(trimmed.substr(0, eq_pos)));
    auto value_str = std::string(trim(trimmed.substr(eq_pos + 1)));

    // KID only has one entry type: Keyword
    std::string type_lower = type_str;
    std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (type_lower != "keyword") {
        return {};
    }

    // Split by pipes: KeywordFormOrEditorID | ItemType | StringOrFormFilters | TraitFilters | Chance
    auto fields = split_pipes(value_str);
    if (fields.empty()) return {};

    // Field 0: keyword to assign (FormOrEditorID)
    auto keyword_ref = parse_form_ref(fields[0]);
    std::string keyword_name = resolve_symbol(keyword_ref);

    // Field 1: item type
    std::string item_type;
    if (fields.size() > 1) {
        item_type = fields[1];
    }
    std::string relation = item_type_to_relation(item_type);
    if (relation.empty() && !item_type.empty()) {
        // Unknown item type — emit diagnostic but still attempt to build a rule
        diags_.warning("W_KID_UNKNOWN_TYPE",
                       "Unknown KID item type '" + item_type + "', skipping",
                       {filename, static_cast<uint32_t>(line_num), 0,
                        static_cast<uint32_t>(line_num), 0},
                       "");
        return {};
    }

    // Sanitize name for valid identifier
    auto sanitize = [](const std::string& s) {
        std::string r;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            else if (c == '.' || c == '|' || c == '/' || c == ' ' || c == '-')
                if (!r.empty() && r.back() != '_') r += '_';
        }
        while (!r.empty() && r.back() == '_') r.pop_back();
        return r.empty() ? std::string("unknown") : r;
    };
    std::string keyword_clean = sanitize(keyword_name);
    std::string rule_name = "add_" + keyword_clean + "_to_" + relation
                            + "_L" + std::to_string(line_num);

    SourceSpan span{filename, static_cast<uint32_t>(line_num), 0,
                    static_cast<uint32_t>(line_num), 0};

    Rule rule;
    rule.name = pool_.intern(rule_name);
    rule.head_args.push_back(make_var("Item"));
    rule.span = span;

    // Item type clause: armor(Item), weapon(Item), etc.
    if (!relation.empty()) {
        std::vector<Expr> type_args;
        type_args.push_back(make_var("Item"));
        rule.body.push_back(make_fact(relation, std::move(type_args)));
    }

    // Field 2: string/form filters
    if (fields.size() > 2 && !fields[2].empty()) {
        add_item_filters(fields[2], "Item", rule.body);
    }

    // Field 3: trait filters (not implemented in Phase 1, reserved)
    // Field 4: chance (not implemented in Phase 1, reserved)

    // Effect: => add_keyword(Item, :KeywordName)
    Effect effect;
    effect.action = pool_.intern("add_keyword");
    effect.args.push_back(make_var("Item"));
    effect.args.push_back(make_sym(keyword_name));
    effect.span = span;
    rule.effects.push_back(std::move(effect));

    std::vector<Rule> result;
    result.push_back(std::move(rule));
    return result;
}

std::vector<Rule> KidParser::parse_string(const std::string& content,
                                           const std::string& filename) {
    std::vector<Rule> all_rules;
    std::istringstream stream(content);
    std::string line;
    int line_num = 0;

    while (std::getline(stream, line)) {
        ++line_num;
        auto rules = parse_line(line, filename, line_num);
        for (auto& r : rules) {
            all_rules.push_back(std::move(r));
        }
    }
    return all_rules;
}

std::vector<Rule> KidParser::parse_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        diags_.error("E_KID_FILE", "Cannot open file: " + path, {}, "");
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parse_string(content, path);
}

} // namespace mora
