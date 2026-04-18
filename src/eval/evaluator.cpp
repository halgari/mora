#include "mora/eval/evaluator.h"
#include "mora/core/type.h"
#include "mora/eval/rule_planner.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <vector>

namespace mora {

Evaluator::Evaluator(StringPool& pool, DiagBag& diags, const FactDB& db)
    : pool_(pool), diags_(diags), db_(db), derived_facts_(pool) {}

void Evaluator::set_symbol_formid(StringId symbol_name, uint32_t formid) {
    symbol_formids_[symbol_name.index] = formid;
    // Also store with colon prefix, since the lexer keeps the colon in symbol tokens
    std::string const with_colon = ":" + std::string(pool_.get(symbol_name));
    StringId const colon_id = pool_.intern(with_colon);
    symbol_formids_[colon_id.index] = formid;
}

void Evaluator::ensure_effect_relations_configured(FactDB& db) {
    if (effect_rels_configured_) return;
    effect_rel_set_      = pool_.intern("skyrim/set");
    effect_rel_add_      = pool_.intern("skyrim/add");
    effect_rel_remove_   = pool_.intern("skyrim/remove");
    effect_rel_multiply_ = pool_.intern("skyrim/multiply");

    auto const* formid_t = TypeRegistry::instance().find("FormID");
    // FormID must have been registered by Skyrim at this point. If not,
    // fall back to Any — preserves the FormID Value kind through materialize().
    // (Int32 fallback loses the kind on Column::at round-trip.)
    if (formid_t == nullptr) formid_t = types::any();

    std::vector<const Type*> effect_cols = {
        formid_t,         // col 0: target FormID
        types::keyword(), // col 1: field keyword
        types::any(),     // col 2: polymorphic value
    };

    for (StringId rel : {effect_rel_set_, effect_rel_add_,
                         effect_rel_remove_, effect_rel_multiply_}) {
        db.configure_relation(rel, effect_cols, /*indexed*/ {0});
    }
    effect_rels_configured_ = true;
}

void Evaluator::evaluate_module(const Module& mod, FactDB& out_facts,
                                  ProgressCallback progress) {
    ensure_effect_relations_configured(out_facts);
    for (size_t i = 0; i < mod.rules.size(); ++i) {
        const Rule& rule = mod.rules[i];
        evaluate_rule(rule, out_facts);
        if (progress) progress(i + 1, mod.rules.size(), pool_.get(rule.name));
    }
}

void Evaluator::evaluate_rule(const Rule& rule, FactDB& db) {
    auto plan = plan_rule(rule, db_, derived_facts_, pool_, symbol_formids_);
    if (!plan) {
        diags_.error("eval-unsupported",
                      std::string("internal: vectorized planner declined rule '") +
                          std::string(pool_.get(rule.name)) + "'",
                      rule.span, "");
        return;
    }
    for (auto& op : plan->effect_ops) op->run(db);
    if (plan->derived_op) plan->derived_op->run(derived_facts_);
}

} // namespace mora
