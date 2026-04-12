#include "mora/eval/evaluator.h"
#include <algorithm>
#include <cassert>
#include <chrono>

namespace mora {

Evaluator::Evaluator(StringPool& pool, DiagBag& diags, const FactDB& db)
    : pool_(pool), diags_(diags), db_(db), derived_facts_(pool) {}

void Evaluator::set_symbol_formid(StringId symbol_name, uint32_t formid) {
    symbol_formids_[symbol_name.index] = formid;
    // Also store with colon prefix, since the lexer keeps the colon in symbol tokens
    std::string with_colon = ":" + std::string(pool_.get(symbol_name));
    StringId colon_id = pool_.intern(with_colon);
    symbol_formids_[colon_id.index] = formid;
}

PatchSet Evaluator::evaluate_static(const Module& mod,
                                     ProgressCallback progress) {
    PatchSet patches;
    PhaseClassifier classifier(pool_);

    if (mod.ns) {
        current_mod_name_ = mod.ns->name;
    } else {
        current_mod_name_ = pool_.intern("anonymous");
    }

    // Count static rules for progress reporting
    size_t total_static = 0;
    for (const Rule& rule : mod.rules) {
        if (classifier.classify(rule) == Phase::Static) total_static++;
    }

    size_t done = 0;
    auto report = [&](const Rule& rule) {
        ++done;
        if (progress) progress(done, total_static, pool_.get(rule.name));
    };

    // First pass: evaluate derived rules (no effects) to populate derived_facts_
    for (size_t i = 0; i < mod.rules.size(); ++i) {
        const Rule& rule = mod.rules[i];
        if (classifier.classify(rule) != Phase::Static) continue;
        if (rule.effects.empty() && rule.conditional_effects.empty()) {
            evaluate_rule(rule, patches, static_cast<uint32_t>(i));
            report(rule);
        }
    }

    // Second pass: evaluate rules with effects to produce patches
    for (size_t i = 0; i < mod.rules.size(); ++i) {
        const Rule& rule = mod.rules[i];
        if (classifier.classify(rule) != Phase::Static) continue;
        if (!rule.effects.empty() || !rule.conditional_effects.empty()) {
            evaluate_rule(rule, patches, static_cast<uint32_t>(i));
            report(rule);
        }
    }

    return patches;
}

// Score a clause for ordering. Lower = more selective = should run first.
// Clauses with more constants/symbols drive indexed lookups and produce
// smaller intermediate result sets.
std::vector<size_t> Evaluator::compute_clause_order(const Rule& rule) const {
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

void Evaluator::evaluate_rule(const Rule& rule, PatchSet& patches, uint32_t priority) {
    Bindings bindings;
    auto order = compute_clause_order(rule);
    match_clauses(rule, order, 0, bindings, patches, priority);
}

void Evaluator::match_clauses(const Rule& rule, const std::vector<size_t>& order,
                               size_t step, Bindings& bindings, PatchSet& patches,
                               uint32_t priority) {
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
            apply_effects(rule, bindings, patches, priority);
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
                    match_clauses(rule, order, step + 1, bindings, patches, priority);
                }
            } else {
                // Positive pattern: find all matches and recurse for each
                auto all_bindings = match_fact_pattern(c, bindings);
                for (auto& new_bindings : all_bindings) {
                    match_clauses(rule, order, step + 1, new_bindings, patches, priority);
                }
            }
        } else if constexpr (std::is_same_v<T, GuardClause>) {
            if (evaluate_guard(*c.expr, bindings)) {
                match_clauses(rule, order, step + 1, bindings, patches, priority);
            }
        } else if constexpr (std::is_same_v<T, Effect>) {
            // Effects in body are handled separately; skip
            match_clauses(rule, order, step + 1, bindings, patches, priority);
        } else if constexpr (std::is_same_v<T, ConditionalEffect>) {
            // Conditional effects in body are handled separately; skip
            match_clauses(rule, order, step + 1, bindings, patches, priority);
        } else if constexpr (std::is_same_v<T, InClause>) {
            Value var_val = resolve_expr(*c.variable, bindings);

            // List-typed RHS from FactDB binding
            if (c.values.size() == 1) {
                Value rhs = resolve_expr(c.values[0], bindings);
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
                                              new_bindings, patches, priority);
                            }
                        }
                    } else {
                        // Bound variable: membership check
                        if (rhs.list_contains(var_val)) {
                            match_clauses(rule, order, step + 1, bindings,
                                          patches, priority);
                        }
                    }
                    return;
                }
            }
            // Original path: literal value list
            for (const auto& val_expr : c.values) {
                Value v = resolve_expr(val_expr, bindings);
                if (var_val.matches(v)) {
                    match_clauses(rule, order, step + 1, bindings, patches, priority);
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
                        match_clauses(rule, order, step + 1, new_bindings, patches, priority);
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
        } else if (const auto* il = std::get_if<IntLiteral>(&arg.data)) {
            query_tuple.push_back(Value::make_int(il->value));
        } else if (const auto* fl = std::get_if<FloatLiteral>(&arg.data)) {
            query_tuple.push_back(Value::make_float(fl->value));
        } else if (const auto* sl = std::get_if<StringLiteral>(&arg.data)) {
            query_tuple.push_back(Value::make_string(sl->value));
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
        bool ok = true;
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
    if (const auto* be = std::get_if<BinaryExpr>(&expr.data)) {
        Value left = resolve_expr(*be->left, bindings);
        Value right = resolve_expr(*be->right, bindings);

        // Compare based on types
        if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
            int64_t l = left.as_int();
            int64_t r = right.as_int();
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::Float && right.kind() == Value::Kind::Float) {
            double l = left.as_float();
            double r = right.as_float();
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Float) {
            double l = static_cast<double>(left.as_int());
            double r = right.as_float();
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::Float && right.kind() == Value::Kind::Int) {
            double l = left.as_float();
            double r = static_cast<double>(right.as_int());
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::FormID && right.kind() == Value::Kind::FormID) {
            switch (be->op) {
                case BinaryExpr::Op::Eq:  return left.as_formid() == right.as_formid();
                case BinaryExpr::Op::Neq: return left.as_formid() != right.as_formid();
                default: break;
            }
        }
    }
    return false;
}

void Evaluator::apply_effects(const Rule& rule, const Bindings& bindings,
                               PatchSet& patches, uint32_t priority) {
    // Unconditional effects
    for (const Effect& effect : rule.effects) {
        auto [field, op] = action_to_field(effect.action);
        if (effect.args.size() < 2) continue;

        Value target = resolve_expr(effect.args[0], bindings);
        Value value = resolve_expr(effect.args[1], bindings);

        if (target.kind() == Value::Kind::FormID) {
            patches.add_patch(target.as_formid(), field, op, value,
                              current_mod_name_, priority);
        }
    }

    // Conditional effects
    for (const ConditionalEffect& ce : rule.conditional_effects) {
        if (evaluate_guard(*ce.guard, bindings)) {
            auto [field, op] = action_to_field(ce.effect.action);
            if (ce.effect.args.size() < 2) continue;

            Value target = resolve_expr(ce.effect.args[0], bindings);
            Value value = resolve_expr(ce.effect.args[1], bindings);

            if (target.kind() == Value::Kind::FormID) {
                patches.add_patch(target.as_formid(), field, op, value,
                                  current_mod_name_, priority);
            }
        }
    }
}

Value Evaluator::resolve_expr(const Expr& expr, const Bindings& bindings) {
    return std::visit([&](const auto& e) -> Value {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, VariableExpr>) {
            auto it = bindings.find(e.name.index);
            if (it != bindings.end()) {
                return it->second;
            }
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, SymbolExpr>) {
            auto it = symbol_formids_.find(e.name.index);
            if (it != symbol_formids_.end()) {
                return Value::make_formid(it->second);
            }
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, IntLiteral>) {
            return Value::make_int(e.value);
        } else if constexpr (std::is_same_v<T, FloatLiteral>) {
            return Value::make_float(e.value);
        } else if constexpr (std::is_same_v<T, StringLiteral>) {
            return Value::make_string(e.value);
        } else if constexpr (std::is_same_v<T, DiscardExpr>) {
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            Value left = resolve_expr(*e.left, bindings);
            Value right = resolve_expr(*e.right, bindings);
            // Arithmetic operations
            if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
                switch (e.op) {
                    case BinaryExpr::Op::Add: return Value::make_int(left.as_int() + right.as_int());
                    case BinaryExpr::Op::Sub: return Value::make_int(left.as_int() - right.as_int());
                    case BinaryExpr::Op::Mul: return Value::make_int(left.as_int() * right.as_int());
                    case BinaryExpr::Op::Div: return Value::make_int(left.as_int() / right.as_int());
                    default: break;
                }
            }
            if (left.kind() == Value::Kind::Float || right.kind() == Value::Kind::Float) {
                double l = (left.kind() == Value::Kind::Float) ? left.as_float() : static_cast<double>(left.as_int());
                double r = (right.kind() == Value::Kind::Float) ? right.as_float() : static_cast<double>(right.as_int());
                switch (e.op) {
                    case BinaryExpr::Op::Add: return Value::make_float(l + r);
                    case BinaryExpr::Op::Sub: return Value::make_float(l - r);
                    case BinaryExpr::Op::Mul: return Value::make_float(l * r);
                    case BinaryExpr::Op::Div: return Value::make_float(l / r);
                    default: break;
                }
            }
            // Boolean comparison results
            // For comparison ops, evaluate and return bool
            if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
                int64_t l = left.as_int(), r = right.as_int();
                switch (e.op) {
                    case BinaryExpr::Op::Eq:   return Value::make_bool(l == r);
                    case BinaryExpr::Op::Neq:  return Value::make_bool(l != r);
                    case BinaryExpr::Op::Lt:   return Value::make_bool(l < r);
                    case BinaryExpr::Op::Gt:   return Value::make_bool(l > r);
                    case BinaryExpr::Op::LtEq: return Value::make_bool(l <= r);
                    case BinaryExpr::Op::GtEq: return Value::make_bool(l >= r);
                    default: break;
                }
            }
            return Value::make_var();
        } else {
            return Value::make_var();
        }
    }, expr.data);
}

std::pair<FieldId, FieldOp> Evaluator::action_to_field(StringId action) const {
    auto name = pool_.get(action);

    // Form list add/remove
    if (name == "add_keyword")      return {FieldId::Keywords,    FieldOp::Add};
    if (name == "remove_keyword")   return {FieldId::Keywords,    FieldOp::Remove};
    if (name == "add_spell")        return {FieldId::Spells,      FieldOp::Add};
    if (name == "remove_spell")     return {FieldId::Spells,      FieldOp::Remove};
    if (name == "add_perk")         return {FieldId::Perks,       FieldOp::Add};
    if (name == "add_faction")      return {FieldId::Factions,    FieldOp::Add};
    if (name == "remove_faction")   return {FieldId::Factions,    FieldOp::Remove};
    if (name == "add_shout")        return {FieldId::Shouts,      FieldOp::Add};
    if (name == "remove_shout")     return {FieldId::Shouts,      FieldOp::Remove};
    if (name == "add_item")         return {FieldId::Items,       FieldOp::Add};
    if (name == "add_lev_spell")    return {FieldId::LevSpells,   FieldOp::Add};

    // Scalar set
    if (name == "set_name")          return {FieldId::Name,         FieldOp::Set};
    if (name == "set_damage")        return {FieldId::Damage,       FieldOp::Set};
    if (name == "set_armor_rating")  return {FieldId::ArmorRating,  FieldOp::Set};
    if (name == "set_gold_value")    return {FieldId::GoldValue,    FieldOp::Set};
    if (name == "set_weight")        return {FieldId::Weight,       FieldOp::Set};
    if (name == "set_speed")         return {FieldId::Speed,        FieldOp::Set};
    if (name == "set_reach")         return {FieldId::Reach,        FieldOp::Set};
    if (name == "set_stagger")       return {FieldId::Stagger,      FieldOp::Set};
    if (name == "set_range_min")     return {FieldId::RangeMin,     FieldOp::Set};
    if (name == "set_range_max")     return {FieldId::RangeMax,     FieldOp::Set};
    if (name == "set_crit_damage")   return {FieldId::CritDamage,   FieldOp::Set};
    if (name == "set_crit_percent")  return {FieldId::CritPercent,  FieldOp::Set};
    if (name == "set_health")        return {FieldId::Health,       FieldOp::Set};
    if (name == "set_level")         return {FieldId::Level,        FieldOp::Set};
    if (name == "set_calc_level_min") return {FieldId::CalcLevelMin, FieldOp::Set};
    if (name == "set_calc_level_max") return {FieldId::CalcLevelMax, FieldOp::Set};
    if (name == "set_speed_mult")    return {FieldId::SpeedMult,    FieldOp::Set};
    if (name == "set_game_setting")  return {FieldId::GoldValue,    FieldOp::Set};

    // Scalar multiply
    if (name == "mul_damage")        return {FieldId::Damage,       FieldOp::Multiply};
    if (name == "mul_armor_rating")  return {FieldId::ArmorRating,  FieldOp::Multiply};
    if (name == "mul_gold_value")    return {FieldId::GoldValue,    FieldOp::Multiply};
    if (name == "mul_weight")        return {FieldId::Weight,       FieldOp::Multiply};
    if (name == "mul_speed")         return {FieldId::Speed,        FieldOp::Multiply};
    if (name == "mul_crit_percent")  return {FieldId::CritPercent,  FieldOp::Multiply};

    // Form references
    if (name == "set_race")          return {FieldId::RaceForm,        FieldOp::Set};
    if (name == "set_class")         return {FieldId::ClassForm,       FieldOp::Set};
    if (name == "set_skin")          return {FieldId::SkinForm,        FieldOp::Set};
    if (name == "set_outfit")        return {FieldId::OutfitForm,      FieldOp::Set};
    if (name == "set_enchantment")   return {FieldId::EnchantmentForm, FieldOp::Set};
    if (name == "set_voice_type")    return {FieldId::VoiceTypeForm,   FieldOp::Set};

    // Boolean flags
    if (name == "set_essential")       return {FieldId::Essential,      FieldOp::Set};
    if (name == "set_protected")       return {FieldId::Protected,      FieldOp::Set};
    if (name == "set_auto_calc_stats") return {FieldId::AutoCalcStats,  FieldOp::Set};
    if (name == "clear_all")           return {FieldId::ClearAll,       FieldOp::Set};

    // Default fallback
    return {FieldId::Keywords, FieldOp::Add};
}

} // namespace mora
