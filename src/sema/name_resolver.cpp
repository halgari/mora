#include "mora/sema/name_resolver.h"
#include "mora/data/form_model.h"
#include <variant>

namespace mora {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FactSignature make_sig(StringId name, std::vector<MoraType> types) {
    return FactSignature{name, std::move(types)};
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

NameResolver::NameResolver(StringPool& pool, DiagBag& diags)
    : pool_(pool), diags_(diags) {
    register_builtins();
}

// ---------------------------------------------------------------------------
// register_builtins — all built-in Skyrim facts
// ---------------------------------------------------------------------------

void NameResolver::register_builtins() {
    // Helper lambda to register one fact by name string + type list
    auto reg = [&](const char* n, std::vector<MoraType> types) {
        StringId id = pool_.intern(n);
        facts_.emplace(id.index, make_sig(id, std::move(types)));
    };

    using T = TypeKind;
    auto t = [](TypeKind k) { return MoraType::make(k); };

    // ── Existence facts ──
    reg("npc",          { t(T::NpcID)    });
    reg("weapon",       { t(T::WeaponID) });
    reg("armor",        { t(T::ArmorID)  });
    reg("spell",        { t(T::SpellID)  });
    reg("perk",         { t(T::PerkID)   });
    reg("keyword",      { t(T::KeywordID)});
    reg("faction",      { t(T::FactionID)});
    reg("race",         { t(T::RaceID)   });
    reg("leveled_list", { t(T::FormID)   });
    reg("ammo",         { t(T::FormID)   });
    reg("book",         { t(T::FormID)   });
    reg("potion",       { t(T::FormID)   });
    reg("misc_item",    { t(T::FormID)   });
    reg("magic_effect", { t(T::FormID)   });
    reg("ingredient",   { t(T::FormID)   });
    reg("activator",    { t(T::FormID)   });
    reg("flora",        { t(T::FormID)   });
    reg("scroll",       { t(T::FormID)   });
    reg("soul_gem",     { t(T::FormID)   });
    reg("location",     { t(T::FormID)   });
    reg("key",          { t(T::FormID)   });
    reg("furniture",    { t(T::FormID)   });
    reg("enchantment",  { t(T::FormID)   });

    // ── Property facts ──
    reg("has_keyword",   { t(T::FormID), t(T::KeywordID) });
    reg("has_faction",   { t(T::FormID), t(T::FactionID) });
    reg("has_perk",      { t(T::FormID), t(T::PerkID)    });
    reg("has_spell",     { t(T::FormID), t(T::SpellID)   });
    reg("base_level",    { t(T::FormID), t(T::Int)       });
    reg("level",         { t(T::FormID), t(T::Int)       });
    reg("race_of",       { t(T::FormID), t(T::RaceID)    });
    reg("name",          { t(T::FormID), t(T::String)    });
    reg("editor_id",     { t(T::FormID), t(T::String)    });
    reg("gold_value",    { t(T::FormID), t(T::Int)       });
    reg("weight",        { t(T::FormID), t(T::Float)     });
    reg("damage",        { t(T::FormID), t(T::Int)       });
    reg("armor_rating",  { t(T::FormID), t(T::Int)       });

    // ── Relationships ──
    reg("has_form",      { t(T::FormID), t(T::FormID) });
    reg("template_of",   { t(T::FormID), t(T::FormID) });
    reg("leveled_entry", { t(T::FormID), t(T::FormID), t(T::Int) });
    reg("outfit_has",    { t(T::FormID), t(T::FormID) });

    // ── Instance facts ──
    reg("current_level",    { t(T::FormID), t(T::Int)        });
    reg("current_location", { t(T::FormID), t(T::LocationID) });
    reg("current_cell",     { t(T::FormID), t(T::CellID)     });
    reg("equipped",         { t(T::FormID), t(T::FormID)     });
    reg("in_inventory",     { t(T::FormID), t(T::FormID), t(T::Int) });
    reg("quest_stage",      { t(T::QuestID), t(T::Int)       });
    reg("is_alive",         { t(T::FormID)                   });

    // ── SPID distribution facts ──
    reg("spid_dist",    { t(T::Int), t(T::String), t(T::FormID) });
    reg("spid_filter",  { t(T::Int), t(T::String), t(T::FormID) });
    reg("spid_exclude", { t(T::Int), t(T::String), t(T::FormID) });
    reg("spid_level",   { t(T::Int), t(T::Int), t(T::Int) });

    // ── KID distribution facts ──
    reg("kid_dist",     { t(T::Int), t(T::FormID), t(T::String) });
    reg("kid_filter",   { t(T::Int), t(T::String), t(T::FormID) });
    reg("kid_exclude",  { t(T::Int), t(T::String), t(T::FormID) });

    // ── Effects / actions (auto-registered from form model) ──
    namespace m = model;

    // Scalar field setters: set_gold_value, set_damage, set_speed, etc.
    for (size_t i = 0; i < m::kFieldCount; i++) {
        auto& f = m::kFields[i];
        if (!f.set_action) continue;
        auto& member = m::kComponents[f.component_idx].members[f.member_idx];
        T form_tk = m::effect_form_type_kind(f.component_idx);
        T val_tk = m::value_type_to_type_kind(member.value_type);
        reg(f.set_action, { t(form_tk), t(val_tk) });
    }

    // Form array operations: add_keyword, remove_keyword, add_spell, etc.
    for (size_t i = 0; i < m::kFormArrayCount; i++) {
        auto& fa = m::kFormArrays[i];
        T form_tk = m::effect_form_type_kind(fa.component_idx);
        // Determine value type from the field id
        T val_tk = T::FormID;
        if (fa.field_id == FieldId::Keywords) val_tk = T::KeywordID;
        else if (fa.field_id == FieldId::Spells) val_tk = T::SpellID;
        else if (fa.field_id == FieldId::Perks) val_tk = T::PerkID;
        else if (fa.field_id == FieldId::Factions) val_tk = T::FactionID;

        if (fa.add_action) reg(fa.add_action, { t(form_tk), t(val_tk) });
        if (fa.remove_action) reg(fa.remove_action, { t(form_tk), t(val_tk) });
    }

    // Boolean flag setters: set_essential, set_protected, set_auto_calc_stats
    for (size_t i = 0; i < m::kFlagCount; i++) {
        auto& fl = m::kFlags[i];
        if (!fl.set_action) continue;
        T form_tk = m::effect_form_type_kind(fl.component_idx);
        reg(fl.set_action, { t(form_tk), t(T::Int) });
    }

    // Leveled list operations
    reg("add_to_leveled_list",      { t(T::FormID), t(T::FormID), t(T::Int), t(T::Int) });
    reg("remove_from_leveled_list", { t(T::FormID), t(T::FormID) });
    reg("clear_leveled_list",       { t(T::FormID) });
    reg("clear_all",                { t(T::FormID) });

    // Multiply operations (legacy, pending removal)
    reg("mul_damage",       { t(T::WeaponID), t(T::Float) });
    reg("mul_armor_rating", { t(T::ArmorID),  t(T::Float) });
    reg("mul_gold_value",   { t(T::FormID),   t(T::Float) });
    reg("mul_weight",       { t(T::FormID),   t(T::Float) });
    reg("mul_speed",        { t(T::WeaponID), t(T::Float) });
    reg("mul_crit_percent", { t(T::WeaponID), t(T::Float) });

    // Legacy effects
    reg("add_item",          { t(T::FormID), t(T::FormID)    });
    reg("distribute_items",  { t(T::FormID), t(T::FormID)    });
    reg("set_game_setting",  { t(T::FormID), t(T::Float)     });
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

    // Build a signature based on the arity of the head.  We do not know the
    // exact types yet (that is the type-checker's job), so we use FormID as
    // a universal placeholder for each argument.
    std::vector<MoraType> param_types(rule.head_args.size(),
                                      MoraType::make(TypeKind::FormID));
    FactSignature sig{rule.name, std::move(param_types)};
    facts_.emplace(rule.name.index, std::move(sig));
}

// Check a name/span pair without needing a FactPattern copy.
void NameResolver::check_fact_exists(const FactPattern& pattern) {
    if (lookup_fact(pattern.name) == nullptr) {
        diags_.error("E011",
                     std::string("unknown fact or rule: '") +
                         std::string(pool_.get(pattern.name)) + "'",
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

void NameResolver::resolve_rule(Rule& rule) {
    for (Clause& clause : rule.body) {
        resolve_clause(clause);
    }
    // Also check any top-level effects listed separately on the rule.
    for (const Effect& eff : rule.effects) {
        check_action_name(eff.action, eff.span, *this, diags_, pool_, source_line(eff.span));
    }
    for (const ConditionalEffect& ce : rule.conditional_effects) {
        check_action_name(ce.effect.action, ce.effect.span, *this, diags_, pool_, source_line(ce.effect.span));
    }
}

void NameResolver::resolve_clause(Clause& clause) {
    std::visit([&](auto& node) {
        using NodeT = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeT, FactPattern>) {
            check_fact_exists(node);
        } else if constexpr (std::is_same_v<NodeT, Effect>) {
            check_action_name(node.action, node.span, *this, diags_, pool_, source_line(node.span));
        } else if constexpr (std::is_same_v<NodeT, ConditionalEffect>) {
            check_action_name(node.effect.action, node.effect.span,
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
