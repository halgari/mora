#include "mora/sema/name_resolver.h"
#include "mora/data/form_model.h"
#include "mora/model/relations.h"
#include <unordered_map>
#include <string>
#include <variant>

namespace mora {

// ---------------------------------------------------------------------------
// Import map — built from Module::use_decls
// ---------------------------------------------------------------------------

namespace {

struct ImportMap {
    std::unordered_map<std::string, std::string> alias_to_ns; // "f" -> "form"
    std::unordered_map<std::string, std::string> refer_to_ns; // "keyword" -> "ref"
};

ImportMap build_imports(const Module& m, StringPool& pool, DiagBag& diags,
                                const Module* cur) {
    ImportMap im;
    for (const UseDecl& u : m.use_decls) {
        std::string const ns{pool.get(u.namespace_path)};
        if (u.alias.index != 0) {
            im.alias_to_ns[std::string{pool.get(u.alias)}] = ns;
        }
        for (StringId const name_id : u.refer) {
            std::string const key{pool.get(name_id)};
            auto it = im.refer_to_ns.find(key);
            if (it != im.refer_to_ns.end() && it->second != ns) {
                std::string const line = cur ? cur->get_line(u.span.start_line) : "";
                std::string msg = "name '";
                msg += key;
                msg += "' referred from both '";
                msg += it->second;
                msg += "' and '";
                msg += ns;
                msg += "'";
                diags.error("E013", msg, u.span, line);
            }
            im.refer_to_ns[key] = ns;
        }
    }
    return im;
}

void apply_imports_to_fact(FactPattern& fp, const ImportMap& im,
                                   StringPool& pool) {
    if (fp.qualifier.index == 0) {
        std::string const name{pool.get(fp.name)};
        auto it = im.refer_to_ns.find(name);
        if (it != im.refer_to_ns.end()) {
            fp.qualifier = pool.intern(it->second);
        }
    } else {
        std::string const q{pool.get(fp.qualifier)};
        auto it = im.alias_to_ns.find(q);
        if (it != im.alias_to_ns.end()) {
            fp.qualifier = pool.intern(it->second);
        }
    }
}

void apply_imports_to_effect(Effect& eff, const ImportMap& im,
                                     StringPool& pool) {
    if (eff.namespace_.index == 0) {
        std::string const name{pool.get(eff.name)};
        auto it = im.refer_to_ns.find(name);
        if (it != im.refer_to_ns.end()) {
            eff.namespace_ = pool.intern(it->second);
        }
    } else {
        std::string const q{pool.get(eff.namespace_)};
        auto it = im.alias_to_ns.find(q);
        if (it != im.alias_to_ns.end()) {
            eff.namespace_ = pool.intern(it->second);
        }
    }
}

void apply_imports_to_clause(Clause& clause, const ImportMap& im,
                                     StringPool& pool) {
    std::visit([&](auto& node) {
        using NodeT = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeT, FactPattern>) {
            apply_imports_to_fact(node, im, pool);
        } else if constexpr (std::is_same_v<NodeT, Effect>) {
            apply_imports_to_effect(node, im, pool);
        } else if constexpr (std::is_same_v<NodeT, ConditionalEffect>) {
            apply_imports_to_effect(node.effect, im, pool);
        } else if constexpr (std::is_same_v<NodeT, OrClause>) {
            for (auto& branch : node.branches) {
                for (auto& fp : branch) apply_imports_to_fact(fp, im, pool);
            }
        }
    }, clause.data);
}

} // namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FactSignature make_sig(StringId name, size_t arity, bool is_builtin = false) {
    return FactSignature{name, arity, is_builtin};
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

NameResolver::NameResolver(StringPool& pool, DiagBag& diags)
    : pool_(pool), diags_(diags) {
    register_builtins();
}

// ---------------------------------------------------------------------------
// register_builtins — all built-in Skyrim facts (arity-only; no types)
// ---------------------------------------------------------------------------

void NameResolver::register_builtins() {
    // Helper lambda to register one fact by name string + arity
    auto reg = [&](const char* n, size_t arity) {
        StringId const id = pool_.intern(n);
        facts_.emplace(id.index, make_sig(id, arity, /*is_builtin=*/true));
    };

    // ── Existence facts (arity 1) ──
    reg("npc",          1);
    reg("weapon",       1);
    reg("armor",        1);
    reg("spell",        1);
    reg("perk",         1);
    reg("keyword",      1);
    reg("faction",      1);
    reg("race",         1);
    reg("leveled_list", 1);
    reg("ammo",         1);
    reg("book",         1);
    reg("potion",       1);
    reg("misc_item",    1);
    reg("magic_effect", 1);
    reg("ingredient",   1);
    reg("activator",    1);
    reg("flora",        1);
    reg("scroll",       1);
    reg("soul_gem",     1);
    reg("location",     1);
    reg("key",          1);
    reg("furniture",    1);
    reg("enchantment",  1);

    // ── Property facts (arity 2) ──
    reg("has_keyword",   2);
    reg("has_faction",   2);
    reg("has_perk",      2);
    reg("has_spell",     2);
    reg("base_level",    2);
    reg("level",         2);
    reg("race_of",       2);
    reg("name",          2);
    reg("editor_id",     2);
    reg("gold_value",    2);
    reg("weight",        2);
    reg("damage",        2);
    reg("armor_rating",  2);

    // ── Relationships ──
    reg("has_form",      2);
    reg("template_of",   2);
    reg("leveled_entry", 3);
    reg("outfit_has",    2);

    // ── Instance facts ──
    reg("current_level",    2);
    reg("current_location", 2);
    reg("current_cell",     2);
    reg("equipped",         2);
    reg("in_inventory",     3);
    reg("quest_stage",      2);
    reg("is_alive",         1);

    // ── SPID distribution facts ──
    reg("spid_dist",    3);
    reg("spid_filter",  3);
    reg("spid_exclude", 3);
    reg("spid_level",   3);

    // ── KID distribution facts ──
    reg("kid_dist",     3);
    reg("kid_filter",   3);
    reg("kid_exclude",  3);

    // ── Effects / actions (auto-registered from form model) ──
    namespace m = model;

    // Scalar field setters: set_gold_value, set_damage, set_speed, etc.
    // All setters are binary: (form, value)
    for (size_t i = 0; i < m::kFieldCount; i++) {
        auto& f = m::kFields[i];
        if (!f.set_action) continue;
        reg(f.set_action, 2);
    }

    // Form array operations: add_keyword, remove_keyword, add_spell, etc.
    // All collection ops are binary: (form, item)
    for (size_t i = 0; i < m::kFormArrayCount; i++) {
        auto& fa = m::kFormArrays[i];
        if (fa.add_action)    reg(fa.add_action,    2);
        if (fa.remove_action) reg(fa.remove_action, 2);
    }

    // Boolean flag setters: set_essential, set_protected, set_auto_calc_stats
    // All flag setters are binary: (form, int)
    for (size_t i = 0; i < m::kFlagCount; i++) {
        auto& fl = m::kFlags[i];
        if (!fl.set_action) continue;
        reg(fl.set_action, 2);
    }

    // Leveled list operations
    reg("add_to_leveled_list",      4);
    reg("remove_from_leveled_list", 2);
    reg("clear_leveled_list",       1);
    reg("clear_all",                1);

    // Multiply operations (legacy, pending removal)
    reg("mul_damage",       2);
    reg("mul_armor_rating", 2);
    reg("mul_gold_value",   2);
    reg("mul_weight",       2);
    reg("mul_speed",        2);
    reg("mul_crit_percent", 2);

    // Legacy effects
    reg("add_item",          2);
    reg("distribute_items",  2);
    reg("set_game_setting",  2);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const FactSignature* NameResolver::lookup_fact(StringId name) const {
    auto it = facts_.find(name.index);
    if (it == facts_.end()) return nullptr;
    return &it->second;
}

void NameResolver::resolve(Module& mod) {
    current_mod_ = &mod;

    // Pass 0: build the import map from `use` declarations and rewrite
    // qualifiers on FactPatterns / Effects accordingly.
    ImportMap const imports = build_imports(mod, pool_, diags_, current_mod_);
    for (Rule& rule : mod.rules) {
        for (Clause& clause : rule.body) {
            apply_imports_to_clause(clause, imports, pool_);
        }
        for (Effect& eff : rule.effects) {
            apply_imports_to_effect(eff, imports, pool_);
        }
        for (ConditionalEffect& ce : rule.conditional_effects) {
            apply_imports_to_effect(ce.effect, imports, pool_);
        }
    }

    // Pass 1: register every rule head as a derived fact, detect duplicates.
    for (const Rule& rule : mod.rules) {
        if (diags_.at_error_limit()) break;
        register_rule_as_fact(rule);
    }

    // Pass 2: resolve the body and effects of every rule.
    for (Rule& rule : mod.rules) {
        if (diags_.at_error_limit()) break;
        resolve_rule(rule);
    }

    current_mod_ = nullptr;
}

std::string NameResolver::source_line(const SourceSpan& span) const {
    if (current_mod_) return current_mod_->get_line(span.start_line);
    return "";
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void NameResolver::register_rule_as_fact(const Rule& rule) {
    auto [it, inserted] = rules_.emplace(rule.name.index, true);
    if (!inserted) {
        // Duplicate rule definition
        diags_.error("E012",
                     std::string("duplicate rule definition: '") +
                         std::string(pool_.get(rule.name)) + "'",
                     rule.span, source_line(rule.span));
        return;
    }

    // Build a signature based on the arity of the head only.
    // Sema no longer tracks per-parameter types.
    FactSignature sig{rule.name, rule.head_args.size(), /*is_builtin=*/false};
    facts_.emplace(rule.name.index, std::move(sig));
}

// Check a name/span pair without needing a FactPattern copy.
void NameResolver::check_fact_exists(const FactPattern& pattern) {
    auto arity_msg = [&](std::string_view display_name, size_t expected) {
        return std::string("arity mismatch: '") + std::string(display_name) +
               "' expects " + std::to_string(expected) + " argument(s), got " +
               std::to_string(pattern.args.size());
    };

    std::string_view const ns = pool_.get(pattern.qualifier);
    std::string_view const nm = pool_.get(pattern.name);
    if (!ns.empty()) {
        if (auto const* rel = model::find_relation(
                ns, nm, model::kRelations, model::kRelationCount)) {
            if (pattern.args.size() != rel->arg_count) {
                std::string qname;
                qname += ns;
                qname += '/';
                qname += nm;
                diags_.error("E014", arity_msg(qname, rel->arg_count),
                             pattern.span, source_line(pattern.span));
            }
            return;
        }
    }
    auto const* sig = lookup_fact(pattern.name);
    if (sig == nullptr) {
        diags_.error("E011",
                     std::string("unknown fact or rule: '") +
                         std::string(pool_.get(pattern.name)) + "'",
                     pattern.span, source_line(pattern.span));
        return;
    }
    if (pattern.args.size() != sig->arity) {
        diags_.error("E014", arity_msg(pool_.get(pattern.name), sig->arity),
                     pattern.span, source_line(pattern.span));
    }
}

// Overload that takes name + span directly (avoids copying Expr args).
static void check_action_name(StringId action, const SourceSpan& span,
                               const NameResolver& resolver, DiagBag& diags,
                               StringPool& pool, const std::string& src_line) {
    if (resolver.lookup_fact(action) == nullptr) {
        diags.error("E011",
                    std::string("unknown fact or rule: '") +
                        std::string(pool.get(action)) + "'",
                    span, src_line);
    }
}

// Overload that accepts an Effect's namespace + name — if the namespaced
// relation exists in kRelations, skip the check (type-checker validates).
static void check_effect_name(StringId ns, StringId action, const SourceSpan& span,
                               const NameResolver& resolver, DiagBag& diags,
                               StringPool& pool, const std::string& src_line) {
    std::string_view const ns_sv = pool.get(ns);
    std::string_view const nm_sv = pool.get(action);
    if (!ns_sv.empty()) {
        if (model::find_relation(ns_sv, nm_sv, model::kRelations, model::kRelationCount)) {
            return;
        }
    }
    check_action_name(action, span, resolver, diags, pool, src_line);
}

void NameResolver::resolve_rule(Rule& rule) {
    for (Clause& clause : rule.body) {
        resolve_clause(clause);
    }
    // Also check any top-level effects listed separately on the rule.
    for (const Effect& eff : rule.effects) {
        check_effect_name(eff.namespace_, eff.name, eff.span, *this, diags_, pool_, source_line(eff.span));
    }
    for (const ConditionalEffect& ce : rule.conditional_effects) {
        check_effect_name(ce.effect.namespace_, ce.effect.name, ce.effect.span, *this, diags_, pool_, source_line(ce.effect.span));
    }
}

void NameResolver::resolve_clause(Clause& clause) {
    std::visit([&](auto& node) {
        using NodeT = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeT, FactPattern>) {
            check_fact_exists(node);
        } else if constexpr (std::is_same_v<NodeT, Effect>) {
            check_effect_name(node.namespace_, node.name, node.span, *this, diags_, pool_, source_line(node.span));
        } else if constexpr (std::is_same_v<NodeT, ConditionalEffect>) {
            check_effect_name(node.effect.namespace_, node.effect.name, node.effect.span,
                              *this, diags_, pool_, source_line(node.effect.span));
        } else if constexpr (std::is_same_v<NodeT, OrClause>) {
            for (auto& branch : node.branches) {
                for (auto& fp : branch) check_fact_exists(fp);
            }
        }
        // InClause and GuardClause don't reference facts by name
        // GuardClause — no fact reference to validate here
    }, clause.data);
}

} // namespace mora
