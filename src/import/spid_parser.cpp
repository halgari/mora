#include "mora/import/spid_parser.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mora {

SpidParser::SpidParser(StringPool& pool, DiagBag& diags,
                       const FormIdResolver* resolver)
    : pool_(pool), diags_(diags), resolver_(resolver) {}

std::string SpidParser::resolve_symbol(const FormRef& ref) const {
    if (ref.is_editor_id()) return ref.editor_id;
    // Try resolver first
    if (resolver_ && resolver_->has_data()) {
        auto edid = resolver_->resolve_ref(ref);
        if (!edid.empty()) return edid;
    }
    // Fall back to plugin|hex or just hex
    if (!ref.plugin.empty()) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", ref.form_id);
        return ref.plugin + "|" + buf;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", ref.form_id);
    return std::string(buf);
}

Expr SpidParser::make_var(const char* name) {
    Expr e;
    e.data = VariableExpr{pool_.intern(name), {}, {}};
    return e;
}

Expr SpidParser::make_sym(const std::string& name) {
    Expr e;
    e.data = SymbolExpr{pool_.intern(name), {}, {}};
    return e;
}

Expr SpidParser::make_int(int64_t value) {
    Expr e;
    e.data = IntLiteral{value, {}};
    return e;
}

Clause SpidParser::make_fact(const std::string& fact_name,
                             std::vector<Expr> args, bool negated) {
    FactPattern fp;
    fp.name = pool_.intern(fact_name);
    fp.args = std::move(args);
    fp.negated = negated;
    Clause c;
    c.data = std::move(fp);
    return c;
}

Clause SpidParser::make_guard(BinaryExpr::Op op, Expr left, Expr right) {
    BinaryExpr bin;
    bin.op = op;
    bin.left = std::make_unique<Expr>(std::move(left));
    bin.right = std::make_unique<Expr>(std::move(right));

    Expr guard_expr;
    guard_expr.data = std::move(bin);

    GuardClause gc;
    gc.expr = std::make_unique<Expr>(std::move(guard_expr));

    Clause c;
    c.data = std::move(gc);
    return c;
}

// Determine if a string looks like a keyword EditorID:
// starts with uppercase, no spaces, alphanumeric/underscore only.
static bool looks_like_keyword(const std::string& s) {
    if (s.empty()) return false;
    if (!std::isupper(static_cast<unsigned char>(s[0]))) return false;
    for (char ch : s) {
        if (ch != '_' && !std::isalnum(static_cast<unsigned char>(ch)))
            return false;
    }
    return true;
}

void SpidParser::add_string_filters(const std::string& field,
                                    std::vector<Clause>& body) {
    if (field.empty()) return;
    auto entries = parse_filter_entries(field);
    for (const auto& entry : entries) {
        bool negated = (entry.mode == FilterEntry::Mode::Exclude);
        const std::string& name = entry.ref.editor_id;
        if (name.empty()) continue;

        if (looks_like_keyword(name)) {
            std::vector<Expr> args;
            args.push_back(make_var("NPC"));
            args.push_back(make_sym(name));
            body.push_back(make_fact("has_keyword", std::move(args), negated));
        } else {
            std::vector<Expr> args;
            args.push_back(make_var("NPC"));
            // Use string literal for non-keyword names
            Expr str_expr;
            str_expr.data = StringLiteral{pool_.intern(name), {}};
            args.push_back(std::move(str_expr));
            body.push_back(make_fact("editor_id", std::move(args), negated));
        }
    }
}

void SpidParser::add_form_filters(const std::string& field,
                                  std::vector<Clause>& body) {
    if (field.empty()) return;
    auto entries = parse_filter_entries(field);

    // SPID form filters: comma-separated = OR (any match qualifies).
    // For a single entry or AND entries, emit directly.
    // For multiple OR entries, they are typically race filters.
    // Heuristic: EditorIDs with "Faction" → has_faction,
    //            EditorIDs with "Race" or raw FormIDs → race_of,
    //            else → has_keyword

    for (const auto& entry : entries) {
        bool negated = (entry.mode == FilterEntry::Mode::Exclude);
        const auto& ref = entry.ref;
        std::string sym = resolve_symbol(ref);

        std::vector<Expr> args;
        args.push_back(make_var("NPC"));
        args.push_back(make_sym(sym));

        if (ref.is_editor_id()) {
            if (ref.editor_id.find("Faction") != std::string::npos) {
                body.push_back(make_fact("has_faction", std::move(args), negated));
            } else if (ref.editor_id.find("Race") != std::string::npos) {
                body.push_back(make_fact("race_of", std::move(args), negated));
            } else {
                body.push_back(make_fact("has_keyword", std::move(args), negated));
            }
        } else {
            // Raw FormID — most commonly a race reference in SPID form filters
            body.push_back(make_fact("race_of", std::move(args), negated));
        }
    }
}

void SpidParser::add_level_filters(const std::string& field,
                                   std::vector<Clause>& body) {
    if (field.empty()) return;
    auto range = parse_level_range(field);

    if (range.has_min || range.has_max) {
        // Add base_level(NPC, Level) clause
        std::vector<Expr> args;
        args.push_back(make_var("NPC"));
        args.push_back(make_var("Level"));
        body.push_back(make_fact("base_level", std::move(args)));

        if (range.has_min) {
            body.push_back(make_guard(BinaryExpr::Op::GtEq,
                                      make_var("Level"), make_int(range.min)));
        }
        if (range.has_max) {
            body.push_back(make_guard(BinaryExpr::Op::LtEq,
                                      make_var("Level"), make_int(range.max)));
        }
    }
}

std::vector<Rule> SpidParser::parse_line(const std::string& line,
                                         const std::string& filename,
                                         int line_num) {
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
        return {};
    }

    // Parse "Type = Value|..."
    auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string_view::npos) {
        return {};
    }

    auto type_str = std::string(trim(trimmed.substr(0, eq_pos)));
    auto value_str = std::string(trim(trimmed.substr(eq_pos + 1)));

    // Validate type
    std::string type_lower = type_str;
    std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::string action;
    if (type_lower == "keyword") action = "add_keyword";
    else if (type_lower == "spell") action = "add_spell";
    else if (type_lower == "perk") action = "add_perk";
    else if (type_lower == "item") action = "add_item";
    else {
        return {}; // Unknown type, skip
    }

    // Split by pipes
    auto fields = split_pipes(value_str);
    if (fields.empty()) return {};

    // Field 0: FormOrEditorID (the target)
    auto target_ref = parse_form_ref(fields[0]);
    std::string target_name = resolve_symbol(target_ref);

    // Build rule name
    std::string rule_name = "spid_" + type_lower + "_" + target_name
                            + "_L" + std::to_string(line_num);

    SourceSpan span{filename, static_cast<uint32_t>(line_num), 0,
                    static_cast<uint32_t>(line_num), 0};

    Rule rule;
    rule.name = pool_.intern(rule_name);
    rule.head_args.push_back(make_var("NPC"));
    rule.span = span;

    // Always add npc(NPC)
    {
        std::vector<Expr> args;
        args.push_back(make_var("NPC"));
        rule.body.push_back(make_fact("npc", std::move(args)));
    }

    // Field 1: String filters
    if (fields.size() > 1 && !fields[1].empty()) {
        add_string_filters(fields[1], rule.body);
    }

    // Field 2: Form filters
    if (fields.size() > 2 && !fields[2].empty()) {
        add_form_filters(fields[2], rule.body);
    }

    // Field 3: Level filters
    if (fields.size() > 3 && !fields[3].empty()) {
        add_level_filters(fields[3], rule.body);
    }

    // Field 4: Traits (noted but not fully implemented)
    // Field 5: Count/Index (not implemented in Phase 1)
    // Field 6: Chance (not implemented in Phase 1)

    // Build effect
    Effect effect;
    effect.action = pool_.intern(action);
    effect.args.push_back(make_var("NPC"));
    effect.args.push_back(make_sym(target_name));
    effect.span = span;
    rule.effects.push_back(std::move(effect));

    std::vector<Rule> result;
    result.push_back(std::move(rule));
    return result;
}

std::vector<Rule> SpidParser::parse_string(const std::string& content,
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

std::vector<Rule> SpidParser::parse_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        diags_.error("E_SPID_FILE", "Cannot open file: " + path, {}, "");
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parse_string(content, path);
}

} // namespace mora
