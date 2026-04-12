#include "mora/import/ini_distribution_rules.h"
#include <string>
#include <vector>

namespace mora {

// ── AST construction helpers ────────────────────────────────────

static Expr make_var(StringPool& pool, const char* name) {
    Expr e;
    e.data = VariableExpr{pool.intern(name), {}, {}};
    return e;
}

static Expr make_string_lit(StringPool& pool, const char* str) {
    Expr e;
    e.data = StringLiteral{pool.intern(str), {}};
    return e;
}

static Clause make_fact2(StringPool& pool, const char* name,
                         Expr a0, Expr a1) {
    FactPattern fp;
    fp.name = pool.intern(name);
    fp.args.push_back(std::move(a0));
    fp.args.push_back(std::move(a1));
    Clause c;
    c.data = std::move(fp);
    return c;
}

static Clause make_fact3(StringPool& pool, const char* name,
                         Expr a0, Expr a1, Expr a2) {
    FactPattern fp;
    fp.name = pool.intern(name);
    fp.args.push_back(std::move(a0));
    fp.args.push_back(std::move(a1));
    fp.args.push_back(std::move(a2));
    Clause c;
    c.data = std::move(fp);
    return c;
}

static Clause make_fact1(StringPool& pool, const char* name, Expr a0) {
    FactPattern fp;
    fp.name = pool.intern(name);
    fp.args.push_back(std::move(a0));
    Clause c;
    c.data = std::move(fp);
    return c;
}

static Clause make_in_clause(Expr variable, Expr list_var) {
    InClause ic;
    ic.variable = std::make_unique<Expr>(std::move(variable));
    ic.values.push_back(std::move(list_var));
    Clause c;
    c.data = std::move(ic);
    return c;
}

static Effect make_effect2(StringPool& pool, const char* action,
                           Expr a0, Expr a1) {
    Effect eff;
    eff.action = pool.intern(action);
    eff.args.push_back(std::move(a0));
    eff.args.push_back(std::move(a1));
    return eff;
}

// ── Effect name for distribution type ───────────────────────────

static const char* effect_for_dist_type(const std::string& dist_type) {
    if (dist_type == "keyword") return "add_keyword";
    if (dist_type == "spell")   return "add_spell";
    if (dist_type == "perk")    return "add_perk";
    if (dist_type == "item")    return "add_item";
    // Faction distribution adds NPC to faction — use add_keyword as placeholder
    if (dist_type == "faction") return "add_keyword";
    return "add_keyword";
}

// ── SPID rule generation ────────────────────────────────────────

// Generate a SPID rule for a given (dist_type, filter_kind) pair.
//
// _spid_{dist_type}_by_{filter_kind}(NPC):
//     spid_dist(RuleID, "{dist_type}", Target)
//     spid_filter(RuleID, "{filter_kind}", FilterList)
//     npc(NPC)
//     {join clause for filter_kind}
//     FilterVal in FilterList
//     => {effect}(NPC, Target)
static Rule build_spid_rule(StringPool& pool,
                            const std::string& dist_type,
                            const std::string& filter_kind) {
    std::string rule_name = "_spid_" + dist_type + "_by_" + filter_kind;

    Rule rule;
    rule.name = pool.intern(rule_name);
    rule.head_args.push_back(make_var(pool, "NPC"));

    // spid_dist(RuleID, "{dist_type}", Target)
    rule.body.push_back(make_fact3(pool, "spid_dist",
        make_var(pool, "RuleID"),
        make_string_lit(pool, dist_type.c_str()),
        make_var(pool, "Target")));

    // spid_filter(RuleID, "{filter_kind}", FilterList)
    rule.body.push_back(make_fact3(pool, "spid_filter",
        make_var(pool, "RuleID"),
        make_string_lit(pool, filter_kind.c_str()),
        make_var(pool, "FilterList")));

    // FilterVal in FilterList — iterates list, binds FilterVal
    rule.body.push_back(make_in_clause(
        make_var(pool, "FilterVal"),
        make_var(pool, "FilterList")));

    // Join clause: FilterVal is now bound → indexed lookup on column 1
    // returns only the NPCs that have this specific keyword/race.
    if (filter_kind == "keyword") {
        rule.body.push_back(make_fact2(pool, "has_keyword",
            make_var(pool, "NPC"),
            make_var(pool, "FilterVal")));
    } else if (filter_kind == "form") {
        rule.body.push_back(make_fact2(pool, "race_of",
            make_var(pool, "NPC"),
            make_var(pool, "FilterVal")));
    }

    // npc(NPC) — NPC is now bound from the join → existence check only
    rule.body.push_back(make_fact1(pool, "npc",
        make_var(pool, "NPC")));

    // Effect: {effect}(NPC, Target)
    rule.effects.push_back(make_effect2(pool, effect_for_dist_type(dist_type),
        make_var(pool, "NPC"),
        make_var(pool, "Target")));

    return rule;
}

// ── KID rule generation ─────────────────────────────────────────

// Generate a KID rule for a given item_type.
//
// _kid_{item_type}(Item):
//     kid_dist(RuleID, TargetKW, "{item_type}")
//     {item_type}(Item)
//     kid_filter(RuleID, "keyword", KWList)
//     has_keyword(Item, KW)
//     KW in KWList
//     => add_keyword(Item, TargetKW)
static Rule build_kid_rule(StringPool& pool, const std::string& item_type) {
    std::string rule_name = "_kid_" + item_type;

    Rule rule;
    rule.name = pool.intern(rule_name);
    rule.head_args.push_back(make_var(pool, "Item"));

    // kid_dist(RuleID, TargetKW, "{item_type}")
    rule.body.push_back(make_fact3(pool, "kid_dist",
        make_var(pool, "RuleID"),
        make_var(pool, "TargetKW"),
        make_string_lit(pool, item_type.c_str())));

    // kid_filter(RuleID, "keyword", KWList)
    rule.body.push_back(make_fact3(pool, "kid_filter",
        make_var(pool, "RuleID"),
        make_string_lit(pool, "keyword"),
        make_var(pool, "KWList")));

    // KW in KWList — iterates list, binds KW
    rule.body.push_back(make_in_clause(
        make_var(pool, "KW"),
        make_var(pool, "KWList")));

    // has_keyword(Item, KW) — KW bound → indexed column-1 lookup
    rule.body.push_back(make_fact2(pool, "has_keyword",
        make_var(pool, "Item"),
        make_var(pool, "KW")));

    // {item_type}(Item) — Item bound → existence check
    rule.body.push_back(make_fact1(pool, item_type.c_str(),
        make_var(pool, "Item")));

    // Effect: add_keyword(Item, TargetKW)
    rule.effects.push_back(make_effect2(pool, "add_keyword",
        make_var(pool, "Item"),
        make_var(pool, "TargetKW")));

    return rule;
}

// ── Public API ──────────────────────────────────────────────────

Module build_ini_distribution_rules(StringPool& pool) {
    Module mod;
    mod.filename = "<ini_distribution_rules>";

    // SPID distribution types
    static const std::vector<std::string> spid_dist_types = {
        "keyword", "spell", "perk", "item", "faction"
    };

    // SPID filter kinds
    static const std::vector<std::string> spid_filter_kinds = {
        "keyword", "form"
    };

    // Generate one SPID rule per (dist_type, filter_kind)
    for (const auto& dist_type : spid_dist_types) {
        for (const auto& filter_kind : spid_filter_kinds) {
            mod.rules.push_back(build_spid_rule(pool, dist_type, filter_kind));
        }
    }

    // KID item types
    static const std::vector<std::string> kid_item_types = {
        "weapon", "armor", "ammo", "potion", "book",
        "spell", "misc_item", "magic_effect", "ingredient",
        "scroll", "soul_gem"
    };

    // Generate one KID rule per item_type
    for (const auto& item_type : kid_item_types) {
        mod.rules.push_back(build_kid_rule(pool, item_type));
    }

    return mod;
}

} // namespace mora
