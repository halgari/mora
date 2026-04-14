#include "mora/import/kid_parser.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mora {

KidParser::KidParser(StringPool& pool, DiagBag& diags,
                     const FormIdResolver* resolver)
    : pool_(pool), diags_(diags), resolver_(resolver) {}

// Map KID item type string to a relation name.
// Returns empty string for unknown types.
static const std::unordered_map<std::string, std::string> kKidTypeMap = {
    {"weapon", "weapon"}, {"armor", "armor"}, {"ammo", "ammo"},
    {"potion", "potion"}, {"book", "book"}, {"spell", "spell"},
    {"race", "race"}, {"misc item", "misc_item"}, {"magic effect", "magic_effect"},
    {"ingredient", "ingredient"}, {"activator", "activator"}, {"flora", "flora"},
    {"scroll", "scroll"}, {"soul gem", "soul_gem"}, {"location", "location"},
    {"key", "key"}, {"furniture", "furniture"}, {"enchantment", "enchantment"},
};

static std::string item_type_to_relation(const std::string& item_type) {
    std::string lower = item_type;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto it = kKidTypeMap.find(lower);
    return it != kKidTypeMap.end() ? it->second : std::string{};
}

void KidParser::add_item_filters(const std::string& field,
                                  const std::string& item_var,
                                  std::vector<Clause>& body) {
    if (field.empty()) return;
    auto entries = parse_filter_entries(field);

    // KID semantics: multiple non-negated entries = match ANY (OR).
    std::vector<FactPattern> or_branches;
    std::vector<Clause> negated_clauses;

    for (const auto& entry : entries) {
        bool negated = (entry.mode == FilterEntry::Mode::Exclude);
        const auto& ref = entry.ref;

        FactPattern fp;
        fp.negated = negated;
        fp.args.push_back(make_var(item_var.c_str()));

        if (ref.is_editor_id()) {
            fp.name = pool_.intern("has_keyword");
            fp.args.push_back(make_sym(ref.editor_id));
        } else {
            fp.name = pool_.intern("has_form");
            std::string sym = ref.to_mora_symbol();
            if (!sym.empty() && sym[0] == ':') sym = sym.substr(1);
            fp.args.push_back(make_sym(sym));
        }

        if (negated) {
            Clause c;
            c.data = std::move(fp);
            negated_clauses.push_back(std::move(c));
        } else {
            or_branches.push_back(std::move(fp));
        }
    }

    if (or_branches.size() == 1) {
        Clause c;
        c.data = std::move(or_branches[0]);
        body.push_back(std::move(c));
    } else if (or_branches.size() > 1) {
        OrClause oc;
        for (auto& fp : or_branches) {
            std::vector<FactPattern> branch;
            branch.push_back(std::move(fp));
            oc.branches.push_back(std::move(branch));
        }
        Clause c;
        c.data = std::move(oc);
        body.push_back(std::move(c));
    }

    for (auto& nc : negated_clauses) {
        body.push_back(std::move(nc));
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

    std::string keyword_clean = sanitize_name(keyword_name);
    std::string file_stem = sanitize_name(
        std::string(std::string_view(filename).substr(
            filename.find_last_of("/\\") == std::string::npos
                ? 0 : filename.find_last_of("/\\") + 1)));
    std::string rule_name = file_stem + "_" + keyword_clean + "_to_" + relation
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
    effect.name = pool_.intern("add_keyword");
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
