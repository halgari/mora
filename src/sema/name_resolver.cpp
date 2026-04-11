#include "mora/sema/name_resolver.h"
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

    // ── Effects / actions ──
    reg("add_keyword",       { t(T::FormID), t(T::KeywordID) });
    reg("add_item",          { t(T::FormID), t(T::FormID)    });
    reg("add_spell",         { t(T::FormID), t(T::SpellID)   });
    reg("add_perk",          { t(T::FormID), t(T::PerkID)    });
    reg("remove_keyword",    { t(T::FormID), t(T::KeywordID) });
    reg("set_name",          { t(T::FormID), t(T::String)    });
    reg("set_damage",        { t(T::FormID), t(T::Int)       });
    reg("set_armor_rating",  { t(T::FormID), t(T::Int)       });
    reg("set_gold_value",    { t(T::FormID), t(T::Int)       });
    reg("set_weight",        { t(T::FormID), t(T::Float)     });
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
        register_rule_as_fact(rule);
    }

    // Pass 2: resolve the body and effects of every rule.
    for (Rule& rule : mod.rules) {
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
