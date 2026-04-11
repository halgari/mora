#include "mora/import/skypatcher_parser.h"
#include "mora/import/mora_printer.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mora {

// ── Construction ─────────────────────────────────────────────────────

SkyPatcherParser::SkyPatcherParser(StringPool& pool, DiagBag& diags,
                                     const FormIdResolver* resolver)
    : pool_(pool), diags_(diags), resolver_(resolver) {}

// ── Utility ──────────────────────────────────────────────────────────

std::vector<SkyPatcherParser::KVPair>
SkyPatcherParser::split_kv_pairs(const std::string& line) {
    std::vector<KVPair> result;
    size_t pos = 0;
    while (pos < line.size()) {
        auto colon = line.find(':', pos);
        auto segment = line.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos);
        pos = (colon == std::string::npos) ? line.size() : colon + 1;

        auto eq = segment.find('=');
        if (eq == std::string::npos) continue;

        KVPair kv;
        kv.key = segment.substr(0, eq);
        kv.value = segment.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        };
        trim(kv.key);
        trim(kv.value);

        // Lowercase key
        std::transform(kv.key.begin(), kv.key.end(), kv.key.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (!kv.key.empty() && !kv.value.empty())
            result.push_back(std::move(kv));
    }
    return result;
}

std::vector<std::string> SkyPatcherParser::split_commas(const std::string& value) {
    std::vector<std::string> items;
    std::istringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
        if (!item.empty()) items.push_back(std::move(item));
    }
    return items;
}

std::string SkyPatcherParser::strip_tildes(const std::string& value) {
    auto start = value.find('~');
    auto end = value.rfind('~');
    if (start != std::string::npos && end != std::string::npos && end > start)
        return value.substr(start + 1, end - start - 1);
    return value;
}

FormRef SkyPatcherParser::parse_formref(const std::string& text) const {
    FormRef ref;
    auto pipe = text.find('|');
    if (pipe != std::string::npos) {
        ref.plugin = text.substr(0, pipe);
        auto hex_str = text.substr(pipe + 1);
        // Trim whitespace
        while (!hex_str.empty() && std::isspace(static_cast<unsigned char>(hex_str.front()))) hex_str.erase(hex_str.begin());
        ref.form_id = std::stoul(hex_str, nullptr, 16);
    } else {
        ref.editor_id = text;
    }
    return ref;
}

std::string SkyPatcherParser::resolve_sym(const FormRef& ref) const {
    if (ref.is_editor_id()) return ref.editor_id;
    if (resolver_ && resolver_->has_data()) {
        auto edid = resolver_->resolve_ref(ref);
        if (!edid.empty()) return edid;
    }
    return ref.to_mora_symbol().substr(1); // strip leading ':'
}

std::string SkyPatcherParser::type_from_path(const std::string& path) {
    namespace fs = std::filesystem;
    auto parent = fs::path(path).parent_path().filename().string();
    // Lowercase for matching
    std::transform(parent.begin(), parent.end(), parent.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return parent;
}

// ── AST Helpers ──────────────────────────────────────────────────────

Expr SkyPatcherParser::var(const std::string& name) {
    Expr e; e.data = VariableExpr{pool_.intern(name), {}, {}}; return e;
}
Expr SkyPatcherParser::sym(const std::string& name) {
    Expr e; e.data = SymbolExpr{pool_.intern(name), {}, {}}; return e;
}
Expr SkyPatcherParser::intlit(int64_t v) {
    Expr e; e.data = IntLiteral{v, {}}; return e;
}
Expr SkyPatcherParser::floatlit(double v) {
    Expr e; e.data = FloatLiteral{v, {}}; return e;
}
Clause SkyPatcherParser::fact(const std::string& name, std::vector<Expr> args, bool neg) {
    FactPattern fp;
    fp.name = pool_.intern(name);
    fp.args = std::move(args);
    fp.negated = neg;
    Clause c; c.data = std::move(fp); return c;
}

// ── Generic Filter Dispatch ──────────────────────────────────────────

void SkyPatcherParser::emit_filter(Rule& rule, FilterKind kind,
                                     const std::string& value,
                                     const std::string& v) {
    auto items = split_commas(value);
    if (items.empty()) return;

    switch (kind) {
    case FilterKind::KeywordAnd:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("has_keyword", std::move(args)));
        }
        break;

    case FilterKind::KeywordOr:
        if (items.size() == 1) {
            auto ref = parse_formref(items[0]);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("has_keyword", std::move(args)));
        } else {
            // Emit: has_keyword(X, KW) + KW in [...]
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("KW"));
            rule.body.push_back(fact("has_keyword", std::move(args)));
            InClause ic;
            ic.variable = std::make_unique<Expr>(var("KW"));
            for (auto& item : items) {
                auto ref = parse_formref(item);
                ic.values.push_back(sym(resolve_sym(ref)));
            }
            Clause c; c.data = std::move(ic); rule.body.push_back(std::move(c));
        }
        break;

    case FilterKind::KeywordExclude:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("has_keyword", std::move(args), true));
        }
        break;

    case FilterKind::FactionAnd:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("has_faction", std::move(args)));
        }
        break;

    case FilterKind::FactionOr:
        if (items.size() == 1) {
            auto ref = parse_formref(items[0]);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("has_faction", std::move(args)));
        } else {
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("Fac"));
            rule.body.push_back(fact("has_faction", std::move(args)));
            InClause ic;
            ic.variable = std::make_unique<Expr>(var("Fac"));
            for (auto& item : items) {
                auto ref = parse_formref(item);
                ic.values.push_back(sym(resolve_sym(ref)));
            }
            Clause c; c.data = std::move(ic); rule.body.push_back(std::move(c));
        }
        break;

    case FilterKind::FactionExclude:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("has_faction", std::move(args), true));
        }
        break;

    case FilterKind::RaceOr:
        if (items.size() == 1) {
            auto ref = parse_formref(items[0]);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact("race_of", std::move(args)));
        } else {
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("Race"));
            rule.body.push_back(fact("race_of", std::move(args)));
            InClause ic;
            ic.variable = std::make_unique<Expr>(var("Race"));
            for (auto& item : items) {
                auto ref = parse_formref(item);
                ic.values.push_back(sym(resolve_sym(ref)));
            }
            Clause c; c.data = std::move(ic); rule.body.push_back(std::move(c));
        }
        break;

    case FilterKind::DirectTarget:
        // For direct targets, we'd ideally emit editor_id(X, "name") or formid match
        // For now, treat single targets as keyword-style lookup
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v));
            Expr str_expr; str_expr.data = StringLiteral{pool_.intern(resolve_sym(ref)), {}};
            args.push_back(std::move(str_expr));
            rule.body.push_back(fact("editor_id", std::move(args)));
        }
        break;

    case FilterKind::DirectExclude:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v));
            Expr str_expr; str_expr.data = StringLiteral{pool_.intern(resolve_sym(ref)), {}};
            args.push_back(std::move(str_expr));
            rule.body.push_back(fact("editor_id", std::move(args), true));
        }
        break;

    case FilterKind::LevelRange: {
        // levelRange=min~max
        auto tilde = value.find('~');
        if (tilde != std::string::npos) {
            auto min_str = value.substr(0, tilde);
            auto max_str = value.substr(tilde + 1);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("Level"));
            rule.body.push_back(fact("base_level", std::move(args)));
            if (!min_str.empty()) {
                BinaryExpr bin; bin.op = BinaryExpr::Op::GtEq;
                bin.left = std::make_unique<Expr>(var("Level"));
                bin.right = std::make_unique<Expr>(intlit(std::stoi(min_str)));
                Expr ge; ge.data = std::move(bin);
                GuardClause gc; gc.expr = std::make_unique<Expr>(std::move(ge));
                Clause c; c.data = std::move(gc); rule.body.push_back(std::move(c));
            }
            if (!max_str.empty()) {
                BinaryExpr bin; bin.op = BinaryExpr::Op::LtEq;
                bin.left = std::make_unique<Expr>(var("Level"));
                bin.right = std::make_unique<Expr>(intlit(std::stoi(max_str)));
                Expr le; le.data = std::move(bin);
                GuardClause gc; gc.expr = std::make_unique<Expr>(std::move(le));
                Clause c; c.data = std::move(gc); rule.body.push_back(std::move(c));
            }
        }
        break;
    }

    case FilterKind::EditorIdAnd:
    case FilterKind::EditorIdOr:
    case FilterKind::EditorIdExclude:
    case FilterKind::GenderFilter:
    case FilterKind::SourcePlugin:
    case FilterKind::PluginRequired:
    case FilterKind::PluginRequiredOr:
        // These can't be cleanly expressed in Datalog yet — skip silently
        break;
    }
}

// ── Generic Operation Dispatch ───────────────────────────────────────

static std::string sky_field_to_action(SkyField field, OpKind kind) {
    // Map (field, kind) to a Mora action name
    if (kind == OpKind::AddFormList) {
        switch (field) {
            case SkyField::Keywords: return "add_keyword";
            case SkyField::Spells:   return "add_spell";
            case SkyField::Perks:    return "add_perk";
            case SkyField::Shouts:   return "add_shout";
            default: return "";
        }
    }
    if (kind == OpKind::RemoveFormList) {
        switch (field) {
            case SkyField::Keywords: return "remove_keyword";
            case SkyField::Spells:   return "remove_spell";
            default: return "";
        }
    }
    if (kind == OpKind::SetInt || kind == OpKind::SetFloat || kind == OpKind::AddInt || kind == OpKind::MulFloat) {
        switch (field) {
            case SkyField::Damage:      return "set_damage";
            case SkyField::ArmorRating: return "set_armor_rating";
            case SkyField::GoldValue:   return "set_gold_value";
            case SkyField::Weight:      return "set_weight";
            case SkyField::Speed:       return "set_speed";
            case SkyField::Level:       return "set_level";
            default: return "";
        }
    }
    if (kind == OpKind::SetName) return "set_name";
    if (kind == OpKind::SetForm) return "set_form";
    return "";
}

void SkyPatcherParser::emit_operation(Rule& rule, OpKind kind, SkyField field,
                                        const std::string& value,
                                        const std::string& v,
                                        const SourceSpan& span) {
    switch (kind) {
    case OpKind::SetInt: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        Effect eff; eff.action = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(intlit(std::stoi(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::SetFloat: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        Effect eff; eff.action = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(floatlit(std::stof(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::AddInt:
    case OpKind::MulFloat: {
        // For multipliers/additions, emit as set with a note
        // (full multiplicative/additive logic needs runtime support)
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        Effect eff; eff.action = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(floatlit(std::stof(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::SetName: {
        std::string name = strip_tildes(value);
        Effect eff; eff.action = pool_.intern("set_name"); eff.span = span;
        eff.args.push_back(var(v));
        Expr str_expr; str_expr.data = StringLiteral{pool_.intern(name), {}};
        eff.args.push_back(std::move(str_expr));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::AddFormList: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        auto items = split_commas(value);
        for (auto& item : items) {
            auto ref = parse_formref(item);
            Effect eff; eff.action = pool_.intern(action); eff.span = span;
            eff.args.push_back(var(v));
            eff.args.push_back(sym(resolve_sym(ref)));
            rule.effects.push_back(std::move(eff));
        }
        break;
    }
    case OpKind::RemoveFormList: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        auto items = split_commas(value);
        for (auto& item : items) {
            auto ref = parse_formref(item);
            Effect eff; eff.action = pool_.intern(action); eff.span = span;
            eff.args.push_back(var(v));
            eff.args.push_back(sym(resolve_sym(ref)));
            rule.effects.push_back(std::move(eff));
        }
        break;
    }
    case OpKind::SetForm:
    case OpKind::ClearFlag:
    case OpKind::SetBool:
    case OpKind::AddToLeveledList:
    case OpKind::RemoveFromLeveledList:
        // Not yet mapped to Mora effects — skip
        break;
    }
}

// ── Main Parse Function ──────────────────────────────────────────────

std::vector<Rule> SkyPatcherParser::parse_line(const std::string& line,
                                                 const RecordSchema& schema,
                                                 const std::string& filename,
                                                 int line_num) {
    // Skip comments and empty lines
    if (line.empty()) return {};
    auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line[first] == ';') return {};

    auto kvs = split_kv_pairs(line);
    if (kvs.empty()) return {};

    SourceSpan span{filename, static_cast<uint32_t>(line_num), 0,
                    static_cast<uint32_t>(line_num), 0};

    // Variable name based on record type
    std::string v = "X";
    if (schema.type_name == "npc") v = "NPC";
    else if (schema.type_name == "weapon") v = "Weapon";
    else if (schema.type_name == "armor") v = "Armor";

    // Sanitize for rule name
    auto sanitize = [](const std::string& s) {
        std::string r;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return r.empty() ? std::string("patch") : r;
    };

    std::string rule_name = "skypatcher_" + sanitize(std::string(schema.type_name))
                            + "_L" + std::to_string(line_num);

    Rule rule;
    rule.name = pool_.intern(rule_name);
    rule.head_args.push_back(var(v));
    rule.span = span;

    // Always add type existence fact
    {
        std::vector<Expr> args; args.push_back(var(v));
        rule.body.push_back(fact(std::string(schema.mora_relation), std::move(args)));
    }

    // Process key-value pairs
    for (auto& [key, value] : kvs) {
        if (auto* filter = schema.find_filter(key)) {
            emit_filter(rule, filter->kind, value, v);
        } else if (auto* op = schema.find_operation(key)) {
            emit_operation(rule, op->kind, op->field, value, v, span);
        }
        // Unknown keys silently skipped
    }

    // Only emit rules that have at least one effect
    if (rule.effects.empty() && rule.conditional_effects.empty()) return {};

    std::vector<Rule> result;
    result.push_back(std::move(rule));
    return result;
}

std::vector<Rule> SkyPatcherParser::parse_line(const std::string& line,
                                                 const std::string& record_type,
                                                 const std::string& filename,
                                                 int line_num) {
    auto* schema = schemas::find_schema(record_type);
    if (!schema) return {};
    return parse_line(line, *schema, filename, line_num);
}

std::vector<Rule> SkyPatcherParser::parse_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return {};

    auto record_type = type_from_path(path);
    auto* schema = schemas::find_schema(record_type);
    if (!schema) return {};

    std::vector<Rule> all;
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        ++line_num;
        auto rules = parse_line(line, *schema, path, line_num);
        for (auto& r : rules) all.push_back(std::move(r));
    }
    return all;
}

std::vector<Rule> SkyPatcherParser::parse_directory(const std::string& base_path) {
    namespace fs = std::filesystem;
    std::vector<Rule> all;

    if (!fs::exists(base_path)) return all;

    for (auto& type_dir : fs::directory_iterator(base_path)) {
        if (!type_dir.is_directory()) continue;

        auto type_name = type_dir.path().filename().string();
        // Lowercase for schema lookup
        std::string type_lower = type_name;
        std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        auto* schema = schemas::find_schema(type_lower);
        if (!schema) continue;

        // Recursively scan for .ini files
        for (auto& entry : fs::recursive_directory_iterator(type_dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext != ".ini") continue;

            // Conditional plugin check: if filename contains .esp/.esl/.esm
            auto fname = entry.path().stem().string(); // strip .ini
            // SkyPatcher logic: if stem contains .esp/.esl/.esm, it's conditional
            // We can't check plugin availability at compile time, so always load
            // (the rules reference the plugin anyway — they'll produce no patches
            // if the plugin's forms aren't in the FactDB)

            auto rules = parse_file(entry.path().string());
            for (auto& r : rules) all.push_back(std::move(r));
        }
    }

    return all;
}

} // namespace mora
