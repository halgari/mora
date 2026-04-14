#include "mora/import/skypatcher_parser.h"
#include "mora/import/mora_printer.h"
#include "mora/data/action_names.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mora {

// ── Construction ─────────────────────────────────────────────────────

SkyPatcherParser::SkyPatcherParser(StringPool& pool, DiagBag& diags,
                                     const FormIdResolver* resolver,
                                     const EditorIdMap* editor_ids)
    : pool_(pool), diags_(diags), resolver_(resolver), editor_ids_(editor_ids) {}

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
    return mora::resolve_symbol(ref, resolver_);
}

std::string SkyPatcherParser::type_from_path(const std::string& path) {
    namespace fs = std::filesystem;
    auto parent = fs::path(path).parent_path().filename().string();
    // Lowercase for matching
    std::transform(parent.begin(), parent.end(), parent.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return parent;
}

// AST helpers are now inline in the header, delegating to ini_common

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
            rule.body.push_back(fact(rel::kHasKeyword, std::move(args)));
        }
        break;

    case FilterKind::KeywordOr:
        if (items.size() == 1) {
            auto ref = parse_formref(items[0]);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact(rel::kHasKeyword, std::move(args)));
        } else {
            // Emit: has_keyword(X, KW) + KW in [...]
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("KW"));
            rule.body.push_back(fact(rel::kHasKeyword, std::move(args)));
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
            rule.body.push_back(fact(rel::kHasKeyword, std::move(args), true));
        }
        break;

    case FilterKind::FactionAnd:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact(rel::kHasFaction, std::move(args)));
        }
        break;

    case FilterKind::FactionOr:
        if (items.size() == 1) {
            auto ref = parse_formref(items[0]);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact(rel::kHasFaction, std::move(args)));
        } else {
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("Fac"));
            rule.body.push_back(fact(rel::kHasFaction, std::move(args)));
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
            rule.body.push_back(fact(rel::kHasFaction, std::move(args), true));
        }
        break;

    case FilterKind::RaceOr:
        if (items.size() == 1) {
            auto ref = parse_formref(items[0]);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(sym(resolve_sym(ref)));
            rule.body.push_back(fact(rel::kRaceOf, std::move(args)));
        } else {
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("Race"));
            rule.body.push_back(fact(rel::kRaceOf, std::move(args)));
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
            rule.body.push_back(fact(rel::kEditorId, std::move(args)));
        }
        break;

    case FilterKind::DirectExclude:
        for (auto& item : items) {
            auto ref = parse_formref(item);
            std::vector<Expr> args; args.push_back(var(v));
            Expr str_expr; str_expr.data = StringLiteral{pool_.intern(resolve_sym(ref)), {}};
            args.push_back(std::move(str_expr));
            rule.body.push_back(fact(rel::kEditorId, std::move(args), true));
        }
        break;

    case FilterKind::LevelRange: {
        // levelRange=min~max
        auto tilde = value.find('~');
        if (tilde != std::string::npos) {
            auto min_str = value.substr(0, tilde);
            auto max_str = value.substr(tilde + 1);
            std::vector<Expr> args; args.push_back(var(v)); args.push_back(var("Level"));
            rule.body.push_back(fact(rel::kBaseLevel, std::move(args)));
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

    case FilterKind::PluginRequired: {
        // hasPlugins=P1,P2 — all must be loaded (AND)
        auto items = split_commas(value);
        for (auto& item : items) {
            auto trimmed = std::string(trim(item));
            if (trimmed.empty()) continue;
            std::vector<Expr> args;
            args.push_back(sym(trimmed));
            rule.body.push_back(fact(rel::kPluginLoaded, std::move(args)));
        }
        break;
    }
    case FilterKind::PluginRequiredOr: {
        // hasPluginsOr=P1,P2 — at least one must be loaded (OR)
        auto items = split_commas(value);
        if (items.size() == 1) {
            auto trimmed = std::string(trim(items[0]));
            if (!trimmed.empty()) {
                std::vector<Expr> args;
                args.push_back(sym(trimmed));
                rule.body.push_back(fact(rel::kPluginLoaded, std::move(args)));
            }
        } else if (items.size() > 1) {
            // OR over plugins: use an InClause with a fresh variable
            InClause in;
            in.variable = std::make_unique<Expr>(var("_Plugin"));
            for (auto& item : items) {
                auto trimmed = std::string(trim(item));
                if (!trimmed.empty()) {
                    in.values.push_back(sym(trimmed));
                }
            }
            Clause in_c;
            in_c.data = std::move(in);
            rule.body.push_back(std::move(in_c));
            std::vector<Expr> args;
            args.push_back(var("_Plugin"));
            rule.body.push_back(fact(rel::kPluginLoaded, std::move(args)));
        }
        break;
    }
    case FilterKind::SourcePlugin: {
        // filterByModNames=Plugin.esp — forms originating from these plugins
        auto items = split_commas(value);
        if (items.size() == 1) {
            auto trimmed = std::string(trim(items[0]));
            if (!trimmed.empty()) {
                std::vector<Expr> args;
                args.push_back(var(v));
                args.push_back(sym(trimmed));
                rule.body.push_back(fact(rel::kFormSource, std::move(args)));
            }
        } else if (items.size() > 1) {
            InClause in;
            in.variable = std::make_unique<Expr>(var("_SrcPlugin"));
            for (auto& item : items) {
                auto trimmed = std::string(trim(item));
                if (!trimmed.empty()) in.values.push_back(sym(trimmed));
            }
            Clause in_c; in_c.data = std::move(in);
            rule.body.push_back(std::move(in_c));
            std::vector<Expr> args;
            args.push_back(var(v));
            args.push_back(var("_SrcPlugin"));
            rule.body.push_back(fact(rel::kFormSource, std::move(args)));
        }
        break;
    }
    case FilterKind::GenderFilter: {
        // filterByGender=male/female — requires npc_gender facts
        auto lower = std::string(value);
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (lower == "male" || lower == "female") {
            std::vector<Expr> args;
            args.push_back(var(v));
            args.push_back(sym(lower));
            rule.body.push_back(fact(rel::kNpcGender, std::move(args)));
        }
        break;
    }
    case FilterKind::EditorIdAnd: {
        if (!editor_ids_) break;
        auto substrings = split_commas(value);
        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        auto make_edid_fact = [&](Expr a, Expr b, bool neg = false) {
            std::vector<Expr> args;
            args.push_back(std::move(a));
            args.push_back(std::move(b));
            return fact(rel::kEditorId, std::move(args), neg);
        };
        for (auto& substr : substrings) {
            auto lower_substr = to_lower(substr);
            std::vector<std::string> matches;
            for (auto& [edid, fid] : *editor_ids_) {
                if (to_lower(edid).find(lower_substr) != std::string::npos)
                    matches.push_back(edid);
            }
            if (matches.empty()) {
                rule.body.push_back(make_edid_fact(var(v), sym("__no_match__")));
            } else if (matches.size() == 1) {
                rule.body.push_back(make_edid_fact(var(v), sym(matches[0])));
            } else {
                InClause in;
                in.variable = std::make_unique<Expr>(var("_EdId"));
                for (auto& m : matches) in.values.push_back(sym(m));
                Clause in_c; in_c.data = std::move(in);
                rule.body.push_back(std::move(in_c));
                rule.body.push_back(make_edid_fact(var(v), var("_EdId")));
            }
        }
        break;
    }
    case FilterKind::EditorIdOr: {
        if (!editor_ids_) break;
        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        auto substrings = split_commas(value);
        std::vector<std::string> all_matches;
        for (auto& substr : substrings) {
            auto lower_substr = to_lower(substr);
            for (auto& [edid, fid] : *editor_ids_) {
                if (to_lower(edid).find(lower_substr) != std::string::npos)
                    all_matches.push_back(edid);
            }
        }
        std::sort(all_matches.begin(), all_matches.end());
        all_matches.erase(std::unique(all_matches.begin(), all_matches.end()), all_matches.end());
        if (all_matches.empty()) {
            std::vector<Expr> args;
            args.push_back(var(v));
            args.push_back(sym("__no_match__"));
            rule.body.push_back(fact(rel::kEditorId, std::move(args)));
        } else {
            InClause in;
            in.variable = std::make_unique<Expr>(var("_EdId"));
            for (auto& m : all_matches) in.values.push_back(sym(m));
            Clause in_c; in_c.data = std::move(in);
            rule.body.push_back(std::move(in_c));
            std::vector<Expr> args;
            args.push_back(var(v));
            args.push_back(var("_EdId"));
            rule.body.push_back(fact(rel::kEditorId, std::move(args)));
        }
        break;
    }
    case FilterKind::EditorIdExclude: {
        if (!editor_ids_) break;
        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return std::tolower(c); });
            return s;
        };
        auto substrings = split_commas(value);
        for (auto& substr : substrings) {
            auto lower_substr = to_lower(substr);
            for (auto& [edid, fid] : *editor_ids_) {
                if (to_lower(edid).find(lower_substr) != std::string::npos) {
                    std::vector<Expr> args;
                    args.push_back(var(v));
                    args.push_back(sym(edid));
                    rule.body.push_back(fact(rel::kEditorId, std::move(args), true));
                }
            }
        }
        break;
    }
    }
}

// ── Generic Operation Dispatch ───────────────────────────────────────

static std::string sky_field_to_action(SkyField field, OpKind kind) {
    using namespace mora::action;
    if (kind == OpKind::AddFormList) {
        switch (field) {
            case SkyField::Keywords:  return kAddKeyword;
            case SkyField::Spells:    return kAddSpell;
            case SkyField::Perks:     return kAddPerk;
            case SkyField::Shouts:    return kAddShout;
            case SkyField::Factions:  return kAddFaction;
            case SkyField::Items:     return kAddItem;
            case SkyField::LevSpells: return kAddLevSpell;
            default: return "";
        }
    }
    if (kind == OpKind::RemoveFormList) {
        switch (field) {
            case SkyField::Keywords:  return kRemoveKeyword;
            case SkyField::Spells:    return kRemoveSpell;
            case SkyField::Shouts:    return kRemoveShout;
            case SkyField::Factions:  return kRemoveFaction;
            default: return "";
        }
    }
    if (kind == OpKind::SetInt || kind == OpKind::SetFloat ||
        kind == OpKind::AddInt || kind == OpKind::MulFloat) {
        switch (field) {
            case SkyField::Damage:       return kSetDamage;
            case SkyField::ArmorRating:  return kSetArmorRating;
            case SkyField::GoldValue:    return kSetGoldValue;
            case SkyField::Weight:       return kSetWeight;
            case SkyField::Speed:        return kSetSpeed;
            case SkyField::Level:        return kSetLevel;
            case SkyField::Reach:        return kSetReach;
            case SkyField::Stagger:      return kSetStagger;
            case SkyField::RangeMin:     return kSetRangeMin;
            case SkyField::RangeMax:     return kSetRangeMax;
            case SkyField::CritDamage:   return kSetCritDamage;
            case SkyField::CritPercent:  return kSetCritPercent;
            case SkyField::Health:       return kSetHealth;
            case SkyField::CalcLevelMin: return kSetCalcLevelMin;
            case SkyField::CalcLevelMax: return kSetCalcLevelMax;
            case SkyField::SpeedMult:    return kSetSpeedMult;
            case SkyField::ChanceNone:  return kSetChanceNone;
            default: return "";
        }
    }
    if (kind == OpKind::SetName) return kSetName;
    if (kind == OpKind::SetForm) {
        switch (field) {
            case SkyField::Race:           return kSetRace;
            case SkyField::Class:          return kSetClass;
            case SkyField::Skin:           return kSetSkin;
            case SkyField::OutfitDefault:  return kSetOutfit;
            case SkyField::Enchantment:    return kSetEnchantment;
            case SkyField::VoiceType:      return kSetVoiceType;
            default: return "";
        }
    }
    if (kind == OpKind::SetBool) {
        switch (field) {
            case SkyField::Essential:     return kSetEssential;
            case SkyField::Protected:     return kSetProtected;
            case SkyField::AutoCalcStats: return kSetAutoCalcStats;
            default: return "";
        }
    }
    if (kind == OpKind::ClearFlag) return kClearAll;
    if (kind == OpKind::AddToLeveledList) return kAddToLeveledList;
    if (kind == OpKind::RemoveFromLeveledList) return kRemoveFromLeveledList;
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
        Effect eff; eff.name = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(intlit(std::stoi(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::SetFloat: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        Effect eff; eff.name = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(floatlit(std::stof(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::AddInt: {
        // Additive: treated as set (adding to base requires knowing base at compile time)
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        Effect eff; eff.name = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(intlit(std::stoi(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::MulFloat: {
        // Multiplicative: emit as mul_* action → FieldOp::Multiply at runtime
        std::string set_action = sky_field_to_action(field, OpKind::SetFloat);
        if (set_action.empty()) break;
        // Convert "set_damage" → "mul_damage"
        std::string action = "mul_" + set_action.substr(4); // strip "set_"
        Effect eff; eff.name = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(floatlit(std::stof(value)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::SetName: {
        std::string name = strip_tildes(value);
        Effect eff; eff.name = pool_.intern(action::kSetName); eff.span = span;
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
            Effect eff; eff.name = pool_.intern(action); eff.span = span;
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
            Effect eff; eff.name = pool_.intern(action); eff.span = span;
            eff.args.push_back(var(v));
            eff.args.push_back(sym(resolve_sym(ref)));
            rule.effects.push_back(std::move(eff));
        }
        break;
    }
    case OpKind::SetForm: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        auto ref = parse_formref(value);
        Effect eff; eff.name = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(sym(resolve_sym(ref)));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::SetBool: {
        std::string action = sky_field_to_action(field, kind);
        if (action.empty()) break;
        bool val = (value == "true" || value == "1" || value == "yes");
        Effect eff; eff.name = pool_.intern(action); eff.span = span;
        eff.args.push_back(var(v));
        eff.args.push_back(intlit(val ? 1 : 0));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::ClearFlag: {
        Effect eff; eff.name = pool_.intern(action::kClearAll); eff.span = span;
        eff.args.push_back(var(v));
        rule.effects.push_back(std::move(eff));
        break;
    }
    case OpKind::AddToLeveledList: {
        // Format: Form~level~count (e.g. Skyrim.esm|0x12345~10~1)
        auto parts = split_commas(value); // comma-separated entries
        for (auto& part : parts) {
            // Split by ~ to extract form, level, count
            auto tilde1 = part.find('~');
            if (tilde1 == std::string::npos) continue;
            std::string form_str = part.substr(0, tilde1);
            std::string rest = part.substr(tilde1 + 1);
            uint16_t level_val = 1, count_val = 1;
            auto tilde2 = rest.find('~');
            if (tilde2 != std::string::npos) {
                level_val = static_cast<uint16_t>(std::stoi(rest.substr(0, tilde2)));
                count_val = static_cast<uint16_t>(std::stoi(rest.substr(tilde2 + 1)));
            } else {
                level_val = static_cast<uint16_t>(std::stoi(rest));
            }
            auto ref = parse_formref(form_str);
            Effect eff;
            eff.name = pool_.intern(action::kAddToLeveledList);
            eff.span = span;
            eff.args.push_back(var(v));
            eff.args.push_back(sym(resolve_sym(ref)));
            eff.args.push_back(intlit(level_val));
            eff.args.push_back(intlit(count_val));
            rule.effects.push_back(std::move(eff));
        }
        break;
    }
    case OpKind::RemoveFromLeveledList: {
        auto parts = split_commas(value);
        for (auto& part : parts) {
            // Strip level/count specifiers — just use the form ref
            auto tilde = part.find('~');
            std::string form_str = (tilde != std::string::npos) ? part.substr(0, tilde) : part;
            auto ref = parse_formref(form_str);
            Effect eff;
            eff.name = pool_.intern(action::kRemoveFromLeveledList);
            eff.span = span;
            eff.args.push_back(var(v));
            eff.args.push_back(sym(resolve_sym(ref)));
            rule.effects.push_back(std::move(eff));
        }
        break;
    }
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

    std::string rule_name = "skypatcher_" + sanitize_name(std::string(schema.type_name))
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
