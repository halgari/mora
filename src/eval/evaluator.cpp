#include "mora/eval/evaluator.h"
#include "mora/core/type.h"
#include "mora/data/action_names.h"
#include "mora/data/form_model.h"
#include "mora/eval/expr_eval.h"
#include "mora/eval/rule_planner.h"
#include "mora/model/builtin_fns.h"
#include "mora/model/field_names.h"
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

// Score a clause for ordering. Lower = more selective = should run first.
// Clauses with more constants/symbols drive indexed lookups and produce
// smaller intermediate result sets.
std::vector<size_t> Evaluator::compute_clause_order(const Rule& rule) {
    struct ClauseScore {
        size_t index;
        int score; // lower is better
    };

    std::vector<ClauseScore> scored;
    scored.reserve(rule.body.size());

    for (size_t i = 0; i < rule.body.size(); i++) {
        int score = 0;
        const auto& clause = rule.body[i];
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) {
                int constants = 0;
                int variables = 0;
                for (const Expr& arg : c.args) {
                    if (std::get_if<SymbolExpr>(&arg.data) ||
                        std::get_if<KeywordLiteral>(&arg.data) ||
                        std::get_if<IntLiteral>(&arg.data) ||
                        std::get_if<FloatLiteral>(&arg.data) ||
                        std::get_if<StringLiteral>(&arg.data)) {
                        constants++;
                    } else {
                        variables++;
                    }
                }
                if (constants > 0 && variables == 0) {
                    score = 0; // all constants — existence check
                } else if (constants > 0) {
                    score = 1; // mixed — indexed lookup
                } else if (c.negated) {
                    score = 10; // negated all-var — must run after binding
                } else {
                    score = 5; // all variables — full scan
                }
            } else if constexpr (std::is_same_v<T, GuardClause>) {
                score = 8; // guards depend on bound variables
            } else if constexpr (std::is_same_v<T, InClause>) {
                // InClause with a list var should run early — it iterates
                // list elements and binds the variable, driving indexed
                // lookups in subsequent clauses.
                score = 2;
            } else {
                score = 9;
            }
        }, clause.data);
        scored.push_back({i, score});
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const ClauseScore& a, const ClauseScore& b) {
                         return a.score < b.score;
                     });

    std::vector<size_t> order;
    order.reserve(scored.size());
    for (auto& s : scored) order.push_back(s.index);
    return order;
}

void Evaluator::evaluate_rule(const Rule& rule, FactDB& db) {
    // Try the vectorized planner first.
    auto plan = plan_rule(rule, db_, derived_facts_, pool_, symbol_formids_);
    if (plan) {
        ++vectorized_rules_count_;
        for (auto& op : plan->effect_ops) op->run(db);
        if (plan->derived_op) plan->derived_op->run(derived_facts_);
        return;
    }
    // Fallback: tuple-based match_clauses.
    Bindings bindings;
    auto order = compute_clause_order(rule);
    match_clauses(rule, order, 0, bindings, db);
}

void Evaluator::match_clauses(const Rule& rule, const std::vector<size_t>& order,
                               size_t step, Bindings& bindings, FactDB& db) {
    if (step >= order.size()) {
        // All clauses matched
        if (rule.effects.empty() && rule.conditional_effects.empty()) {
            // Derived rule: add derived fact using the rule's head args as the tuple
            Tuple tuple;
            for (const Expr& arg : rule.head_args) {
                tuple.push_back(resolve_expr(arg, bindings));
            }
            derived_facts_.add_fact(rule.name, std::move(tuple));
        } else {
            apply_effects(rule, bindings, db);
        }
        return;
    }

    const Clause& clause = rule.body[order[step]];
    std::visit([&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, FactPattern>) {
            if (c.negated) {
                // Negated: check that NO match exists
                // Build query from current bindings
                Tuple query_tuple;
                for (const Expr& arg : c.args) {
                    if (const auto* ve = std::get_if<VariableExpr>(&arg.data)) {
                        auto it = bindings.find(ve->name.index);
                        if (it != bindings.end()) {
                            query_tuple.push_back(it->second);
                        } else {
                            query_tuple.push_back(Value::make_var());
                        }
                    } else if (const auto* se = std::get_if<SymbolExpr>(&arg.data)) {
                        auto it = symbol_formids_.find(se->name.index);
                        if (it != symbol_formids_.end()) {
                            query_tuple.push_back(Value::make_formid(it->second));
                        } else {
                            query_tuple.push_back(Value::make_var());
                        }
                    } else {
                        query_tuple.push_back(resolve_expr(arg, bindings));
                    }
                }

                auto neg_results = merged_query(c.name, query_tuple);
                if (neg_results.empty()) {
                    // No match found, negation succeeds
                    match_clauses(rule, order, step + 1, bindings, db);
                }
            } else {
                // Positive pattern: find all matches and recurse for each
                auto all_bindings = match_fact_pattern(c, bindings);
                for (auto& new_bindings : all_bindings) {
                    match_clauses(rule, order, step + 1, new_bindings, db);
                }
            }
        } else if constexpr (std::is_same_v<T, GuardClause>) {
            if (evaluate_guard(*c.expr, bindings)) {
                match_clauses(rule, order, step + 1, bindings, db);
            }
        } else if constexpr (std::is_same_v<T, Effect>) {
            // Effects in body are handled separately; skip
            match_clauses(rule, order, step + 1, bindings, db);
        } else if constexpr (std::is_same_v<T, ConditionalEffect>) {
            // Conditional effects in body are handled separately; skip
            match_clauses(rule, order, step + 1, bindings, db);
        } else if constexpr (std::is_same_v<T, InClause>) {
            Value const var_val = resolve_expr(*c.variable, bindings);

            // List-typed RHS from FactDB binding
            if (c.values.size() == 1) {
                Value const rhs = resolve_expr(c.values[0], bindings);
                if (rhs.kind() == Value::Kind::List) {
                    if (var_val.is_var()) {
                        // Unbound variable: iterate list elements, bind each,
                        // and recurse. This turns "KW in KWList" into a
                        // generator that drives downstream indexed lookups.
                        const auto* ve = std::get_if<VariableExpr>(
                            &c.variable->data);
                        if (ve) {
                            for (const auto& elem : rhs.as_list()) {
                                Bindings new_bindings = bindings;
                                new_bindings[ve->name.index] = elem;
                                match_clauses(rule, order, step + 1,
                                              new_bindings, db);
                            }
                        }
                    } else {
                        // Bound variable: membership check
                        if (rhs.list_contains(var_val)) {
                            match_clauses(rule, order, step + 1, bindings, db);
                        }
                    }
                    return;
                }
            }
            // Original path: literal value list
            for (const auto& val_expr : c.values) {
                Value const v = resolve_expr(val_expr, bindings);
                if (var_val.matches(v)) {
                    match_clauses(rule, order, step + 1, bindings, db);
                    return;
                }
            }
        } else if constexpr (std::is_same_v<T, OrClause>) {
            // Or clause: try each branch, recurse on first match
            for (const auto& branch : c.branches) {
                // Each branch is a vector<FactPattern>
                // For simplicity, handle single-fact branches (most common)
                if (branch.size() == 1) {
                    auto matches = match_fact_pattern(branch[0], bindings);
                    for (auto& new_bindings : matches) {
                        match_clauses(rule, order, step + 1, new_bindings, db);
                    }
                }
            }
        }
    }, clause.data);
}

std::vector<Tuple> Evaluator::merged_query(StringId relation, const Tuple& pattern) {
    auto results_db = db_.query(relation, pattern);
    auto results_derived = derived_facts_.query(relation, pattern);

    if (results_derived.empty()) return results_db;
    if (results_db.empty()) return results_derived;

    std::vector<Tuple> merged;
    merged.reserve(results_db.size() + results_derived.size());
    merged.insert(merged.end(), std::make_move_iterator(results_db.begin()),
                                std::make_move_iterator(results_db.end()));
    merged.insert(merged.end(), std::make_move_iterator(results_derived.begin()),
                                std::make_move_iterator(results_derived.end()));
    return merged;
}

std::vector<Bindings> Evaluator::match_fact_pattern(const FactPattern& pattern,
                                                      const Bindings& bindings) {
    // Build query tuple
    Tuple query_tuple;
    for (const Expr& arg : pattern.args) {
        if (const auto* ve = std::get_if<VariableExpr>(&arg.data)) {
            auto it = bindings.find(ve->name.index);
            if (it != bindings.end()) {
                query_tuple.push_back(it->second);
            } else {
                query_tuple.push_back(Value::make_var());
            }
        } else if (const auto* se = std::get_if<SymbolExpr>(&arg.data)) {
            auto it = symbol_formids_.find(se->name.index);
            if (it != symbol_formids_.end()) {
                query_tuple.push_back(Value::make_formid(it->second));
            } else {
                query_tuple.push_back(Value::make_var());
            }
        } else if (const auto* ee = std::get_if<EditorIdExpr>(&arg.data)) {
            // `@EditorID` resolves the same way as `:symbol` — both route
            // through `symbol_formids_` which main.cpp populates from
            // `EspReader::editor_id_map()` (lowercase keys). Preserve the
            // user's original case first, then fall back to a lowercased
            // lookup so `@IronSword` and `@ironsword` both resolve.
            auto it = symbol_formids_.find(ee->name.index);
            if (it == symbol_formids_.end()) {
                std::string lower(pool_.get(ee->name));
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                it = symbol_formids_.find(pool_.intern(lower).index);
            }
            if (it != symbol_formids_.end()) {
                query_tuple.push_back(Value::make_formid(it->second));
            } else {
                query_tuple.push_back(Value::make_var());
            }
        } else if (const auto* il = std::get_if<IntLiteral>(&arg.data)) {
            query_tuple.push_back(Value::make_int(il->value));
        } else if (const auto* fl = std::get_if<FloatLiteral>(&arg.data)) {
            query_tuple.push_back(Value::make_float(fl->value));
        } else if (const auto* sl = std::get_if<StringLiteral>(&arg.data)) {
            query_tuple.push_back(Value::make_string(sl->value));
        } else if (const auto* kl = std::get_if<KeywordLiteral>(&arg.data)) {
            // Try to resolve as a FormID symbol first; fall back to opaque keyword.
            auto it = symbol_formids_.find(kl->value.index);
            if (it != symbol_formids_.end()) {
                query_tuple.push_back(Value::make_formid(it->second));
            } else {
                query_tuple.push_back(Value::make_keyword(kl->value));
            }
        } else if (std::get_if<DiscardExpr>(&arg.data)) {
            query_tuple.push_back(Value::make_var());
        } else {
            // Fallback: try resolve_expr
            query_tuple.push_back(resolve_expr(arg, bindings));
        }
    }

    // Query with caching — identical queries across rules return the same result
    auto all_results = merged_query(pattern.name, query_tuple);

    // For each matching tuple, create new bindings
    std::vector<Bindings> result;
    for (const Tuple& tuple : all_results) {
        Bindings new_bindings = bindings;
        bool const ok = true;
        for (size_t i = 0; i < pattern.args.size() && i < tuple.size(); ++i) {
            const Expr& arg = pattern.args[i];
            if (const auto* ve = std::get_if<VariableExpr>(&arg.data)) {
                auto it = new_bindings.find(ve->name.index);
                if (it == new_bindings.end()) {
                    // Bind unbound variable
                    new_bindings[ve->name.index] = tuple[i];
                }
                // Already bound variables were used in the query, so they match
            }
            // Other expr types were matched by the query
        }
        if (ok) {
            result.push_back(std::move(new_bindings));
        }
    }

    return result;
}

bool Evaluator::evaluate_guard(const Expr& expr, const Bindings& bindings) {
    return mora::evaluate_guard(expr, bindings, pool_, symbol_formids_);
}

// Build the legacy action name from verb + bare relation name, e.g.
// VerbKind::Set + "damage" → "set_damage". action_to_field's lookup
// tables are keyed on those prefixed strings (kFields[].set_action,
// kFormArrays[].add_action / .remove_action), so the new namespaced
// parser output (`set form/damage(...)` → effect.name="damage")
// needs this reconstruction before the lookup can succeed.
static const char* verb_prefix(VerbKind v) {
    switch (v) {
        case VerbKind::Set:    return "set_";
        case VerbKind::Add:    return "add_";
        case VerbKind::Sub:    return "sub_";
        case VerbKind::Remove: return "remove_";
    }
    return "set_";
}

void Evaluator::apply_effects(const Rule& rule, const Bindings& bindings,
                               FactDB& db) {
    auto emit_effect = [&](uint32_t formid, FieldId field, FieldOp op,
                           const Value& v) {
        StringId rel;
        switch (op) {
            case FieldOp::Set:      rel = effect_rel_set_;      break;
            case FieldOp::Add:      rel = effect_rel_add_;      break;
            case FieldOp::Remove:   rel = effect_rel_remove_;   break;
            case FieldOp::Multiply: rel = effect_rel_multiply_; break;
            default:                __builtin_unreachable();
        }
        auto kw = Value::make_keyword(pool_.intern(field_id_name(field)));
        db.add_fact(rel, Tuple{Value::make_formid(formid), kw, v});
    };

    // Unconditional effects
    for (const Effect& effect : rule.effects) {
        auto legacy = std::string(verb_prefix(effect.verb)) +
                      std::string(pool_.get(effect.name));
        auto [field, op] = action_to_field(pool_.intern(legacy));
        if (effect.args.size() < 2) continue;

        Value const target = resolve_expr(effect.args[0], bindings);
        Value const value = resolve_expr(effect.args[1], bindings);

        // Leveled list add: pack formid + level + count into a single int
        if (field == FieldId::LeveledEntries && op == FieldOp::Add &&
            effect.args.size() >= 4 && value.kind() == Value::Kind::FormID) {
            Value const level_v = resolve_expr(effect.args[2], bindings);
            Value const count_v = resolve_expr(effect.args[3], bindings);
            uint64_t const packed = value.as_formid()
                | (static_cast<uint64_t>(level_v.as_int()) << 32)
                | (static_cast<uint64_t>(count_v.as_int()) << 48);
            if (target.kind() == Value::Kind::FormID) {
                emit_effect(target.as_formid(), field, op,
                    Value::make_int(static_cast<int64_t>(packed)));
            }
            continue;
        }

        if (target.kind() == Value::Kind::FormID) {
            emit_effect(target.as_formid(), field, op, value);
        }
    }

    // Conditional effects
    for (const ConditionalEffect& ce : rule.conditional_effects) {
        if (evaluate_guard(*ce.guard, bindings)) {
            auto legacy = std::string(verb_prefix(ce.effect.verb)) +
                          std::string(pool_.get(ce.effect.name));
            auto [field, op] = action_to_field(pool_.intern(legacy));
            if (ce.effect.args.size() < 2) continue;

            Value const target = resolve_expr(ce.effect.args[0], bindings);
            Value const value = resolve_expr(ce.effect.args[1], bindings);

            if (target.kind() == Value::Kind::FormID) {
                emit_effect(target.as_formid(), field, op, value);
            }
        }
    }
}

Value Evaluator::resolve_expr(const Expr& expr, const Bindings& bindings) {
    return mora::resolve_expr(expr, bindings, pool_, symbol_formids_);
}

std::pair<FieldId, FieldOp> Evaluator::action_to_field(StringId action_id) const {
    auto [field, op] = mora::action_to_field(action_id, pool_);
    if (field == FieldId::Invalid) {
        // Legacy fallback — unknown actions default to keyword add.
        return {FieldId::Keywords, FieldOp::Add};
    }
    return {field, op};
}

} // namespace mora
