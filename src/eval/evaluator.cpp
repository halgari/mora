#include "mora/eval/evaluator.h"
#include "mora/data/action_names.h"
#include "mora/data/form_model.h"
#include "mora/model/builtin_fns.h"
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

PatchSet Evaluator::evaluate_static(const Module& mod,
                                     ProgressCallback progress) {
    PatchSet patches;
    PhaseClassifier const classifier(pool_);

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
                Value const v = resolve_expr(val_expr, bindings);
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
    if (const auto* be = std::get_if<BinaryExpr>(&expr.data)) {
        Value const left = resolve_expr(*be->left, bindings);
        Value const right = resolve_expr(*be->right, bindings);

        // Compare based on types
        if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
            int64_t const l = left.as_int();
            int64_t const r = right.as_int();
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
            double const l = left.as_float();
            double const r = right.as_float();
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
            double const l = static_cast<double>(left.as_int());
            double const r = right.as_float();
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
            double const l = left.as_float();
            double const r = static_cast<double>(right.as_int());
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
                               PatchSet& patches, uint32_t priority) {
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
                patches.add_patch(target.as_formid(), field, op,
                    Value::make_int(static_cast<int64_t>(packed)),
                    current_mod_name_, priority);
            }
            continue;
        }

        if (target.kind() == Value::Kind::FormID) {
            patches.add_patch(target.as_formid(), field, op, value,
                              current_mod_name_, priority);
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
        } else if constexpr (std::is_same_v<T, EditorIdExpr>) {
            auto it = symbol_formids_.find(e.name.index);
            if (it == symbol_formids_.end()) {
                std::string lower(pool_.get(e.name));
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                it = symbol_formids_.find(pool_.intern(lower).index);
            }
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
            Value const left = resolve_expr(*e.left, bindings);
            Value const right = resolve_expr(*e.right, bindings);
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
                double const l = (left.kind() == Value::Kind::Float) ? left.as_float() : static_cast<double>(left.as_int());
                double const r = (right.kind() == Value::Kind::Float) ? right.as_float() : static_cast<double>(right.as_int());
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
                int64_t l = left.as_int();
                int64_t r = right.as_int();
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
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            // Evaluate all args first.
            std::vector<Value> vs;
            vs.reserve(e.args.size());
            for (const auto& a : e.args) vs.push_back(resolve_expr(a, bindings));

            auto numeric = [&](const Value& v) -> double {
                if (v.kind() == Value::Kind::Float) return v.as_float();
                if (v.kind() == Value::Kind::Int)
                    return static_cast<double>(v.as_int());
                return 0.0;
            };
            auto any_float = [&]() {
                for (const auto& v : vs)
                    if (v.kind() == Value::Kind::Float) return true;
                return false;
            };
            auto make_num = [&](double d) {
                if (any_float()) return Value::make_float(d);
                return Value::make_int(static_cast<int64_t>(d));
            };

            std::string_view const name = pool_.get(e.name);
            if (name == "max" && vs.size() == 2) {
                double a = numeric(vs[0]);
                double b = numeric(vs[1]);
                return make_num(a > b ? a : b);
            }
            if (name == "min" && vs.size() == 2) {
                double a = numeric(vs[0]);
                double b = numeric(vs[1]);
                return make_num(a < b ? a : b);
            }
            if (name == "abs" && vs.size() == 1) {
                double const a = numeric(vs[0]);
                return make_num(a < 0 ? -a : a);
            }
            if (name == "clamp" && vs.size() == 3) {
                double const x = numeric(vs[0]);
                double const lo = numeric(vs[1]);
                double const hi = numeric(vs[2]);
                return make_num(std::clamp(x, lo, hi));
            }
            // Unknown or arity-wrong built-in: return unbound var.
            return Value::make_var();
        } else {
            return Value::make_var();
        }
    }, expr.data);
}

std::pair<FieldId, FieldOp> Evaluator::action_to_field(StringId action_id) const {
    auto name = pool_.get(action_id);

    // Scalar fields: match set_action from the model
    for (size_t i = 0; i < model::kFieldCount; i++) {
        if (model::kFields[i].set_action && name == model::kFields[i].set_action)
            return {model::kFields[i].field_id, FieldOp::Set};
    }

    // Form array fields: match add_action and remove_action from the model
    for (size_t i = 0; i < model::kFormArrayCount; i++) {
        auto& fa = model::kFormArrays[i];
        if (fa.add_action && name == fa.add_action)
            return {fa.field_id, FieldOp::Add};
        if (fa.remove_action && name == fa.remove_action)
            return {fa.field_id, FieldOp::Remove};
    }

    // Boolean flags: match set_action
    for (size_t i = 0; i < model::kFlagCount; i++) {
        if (model::kFlags[i].set_action && name == model::kFlags[i].set_action)
            return {model::kFlags[i].field_id, FieldOp::Set};
    }

    // Scalar multiply (kept for backward compat during migration)
    using namespace mora::action;
    if (name == kMulDamage)        return {FieldId::Damage,       FieldOp::Multiply};
    if (name == kMulArmorRating)   return {FieldId::ArmorRating,  FieldOp::Multiply};
    if (name == kMulGoldValue)     return {FieldId::GoldValue,    FieldOp::Multiply};
    if (name == kMulWeight)        return {FieldId::Weight,       FieldOp::Multiply};
    if (name == kMulSpeed)         return {FieldId::Speed,        FieldOp::Multiply};
    if (name == kMulCritPercent)   return {FieldId::CritPercent,  FieldOp::Multiply};

    // Leveled list operations (special, not in scalar model)
    if (name == kAddToLeveledList)      return {FieldId::LeveledEntries, FieldOp::Add};
    if (name == kRemoveFromLeveledList) return {FieldId::LeveledEntries, FieldOp::Remove};
    if (name == kClearLeveledList)      return {FieldId::LeveledEntries, FieldOp::Set};

    // Legacy: add_item, add_lev_spell, set_game_setting, clear_all
    if (name == kAddItem)         return {FieldId::Items,       FieldOp::Add};
    if (name == kAddLevSpell)     return {FieldId::LevSpells,   FieldOp::Add};
    if (name == kSetGameSetting)  return {FieldId::GoldValue,   FieldOp::Set};
    if (name == kClearAll)        return {FieldId::ClearAll,    FieldOp::Set};

    // Default fallback
    return {FieldId::Keywords, FieldOp::Add};
}

} // namespace mora
