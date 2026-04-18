#include "mora/eval/rule_planner.h"

#include "mora/data/action_names.h"
#include "mora/eval/field_types.h"
#include "mora/eval/op_filter.h"
#include "mora/eval/op_join.h"
#include "mora/eval/op_scan.h"
#include "mora/eval/op_union.h"
#include "mora/model/field_names.h"

#include <algorithm>
#include <string>

namespace mora {
namespace {

// Predicates on AST shapes -----------------------------------------------

bool is_simple_arg_expr(const Expr& e) {
    return std::holds_alternative<VariableExpr>(e.data)
        || std::holds_alternative<IntLiteral>(e.data)
        || std::holds_alternative<FloatLiteral>(e.data)
        || std::holds_alternative<StringLiteral>(e.data)
        || std::holds_alternative<KeywordLiteral>(e.data)
        || std::holds_alternative<SymbolExpr>(e.data);
}

// Returns true iff every body clause is either:
// - a positive (non-negated) FactPattern with only simple args, or
// - a GuardClause (M2: guards are supported via FilterOp).
// InClause, negated patterns → false (M3).
bool body_is_supported_for_vectorized(const Rule& rule) {
    // Must have at least one FactPattern to scan.
    bool has_fact_pattern = false;
    for (auto const& clause : rule.body) {
        bool ok = std::visit([](auto const& c) -> bool {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) {
                return !c.negated;
            }
            if constexpr (std::is_same_v<T, GuardClause>) {
                return true;  // M2: guards are supported
            }
            return false;  // InClause, negated, OrClause → reject
        }, clause.data);
        if (!ok) return false;

        // Verify all FactPattern args are simple.
        if (auto const* fp = std::get_if<FactPattern>(&clause.data)) {
            for (auto const& a : fp->args) {
                if (!is_simple_arg_expr(a)) return false;
            }
            has_fact_pattern = true;
        }
    }
    return has_fact_pattern;
}

// Selectivity score for a FactPattern: count non-variable args.
// More constants → lower score → run first.
int selectivity_score(const FactPattern& fp) {
    int constants = 0;
    for (auto const& arg : fp.args) {
        if (!std::holds_alternative<VariableExpr>(arg.data)) ++constants;
    }
    // Negate so that "more constants" = lower (better) score for sort ascending.
    return -constants;
}

// Compute the intersection of two var-name vectors, preserving order of `a`.
std::vector<StringId> var_intersection(const std::vector<StringId>& a,
                                        const std::vector<StringId>& b)
{
    std::vector<StringId> result;
    for (auto const& x : a) {
        for (auto const& y : b) {
            if (x.index == y.index) {
                result.push_back(x);
                break;
            }
        }
    }
    return result;
}

// Union of two var-name vectors, preserving order and deduplicating.
std::vector<StringId> var_union(const std::vector<StringId>& a,
                                 const std::vector<StringId>& b)
{
    std::vector<StringId> result = a;
    for (auto const& y : b) {
        bool found = false;
        for (auto const& x : a) {
            if (x.index == y.index) { found = true; break; }
        }
        if (!found) result.push_back(y);
    }
    return result;
}

// Build an EffectArgSpec from an Expr, resolving KeywordLiterals through
// symbol_formids to match the tuple-path Evaluator::resolve_expr semantics.
EffectArgSpec spec_from_expr(const Expr& e,
                              StringPool& pool,
                              const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    (void)pool;  // reserved for future use (e.g. StringLiteral interning)
    EffectArgSpec s{};
    if (auto const* ve = std::get_if<VariableExpr>(&e.data)) {
        s.kind     = EffectArgSpec::Kind::Var;
        s.var_name = ve->name;
        return s;
    }
    s.kind = EffectArgSpec::Kind::Constant;
    if (auto const* il = std::get_if<IntLiteral>(&e.data)) {
        s.constant = Value::make_int(il->value);
    } else if (auto const* fl = std::get_if<FloatLiteral>(&e.data)) {
        s.constant = Value::make_float(fl->value);
    } else if (auto const* sl = std::get_if<StringLiteral>(&e.data)) {
        s.constant = Value::make_string(sl->value);
    } else if (auto const* kl = std::get_if<KeywordLiteral>(&e.data)) {
        // Task 1.2: match the tuple-path's resolve_expr — if this keyword has
        // an EditorID-to-FormID mapping, prefer the FormID; otherwise keyword.
        auto it = symbols.find(kl->value.index);
        s.constant = (it != symbols.end())
            ? Value::make_formid(it->second)
            : Value::make_keyword(kl->value);
    } else if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        auto it = symbols.find(se->name.index);
        s.constant = (it == symbols.end())
            ? Value::make_var()
            : Value::make_formid(it->second);
    }
    return s;
}

// Build a source operator for a FactPattern, unioning input_db and
// derived_facts when both have the relation (Task 1.1).
std::unique_ptr<Operator> build_source(
    const FactPattern&                            fp,
    const FactDB&                                 input_db,
    const FactDB&                                 derived_facts,
    StringPool&                                   pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    const ColumnarRelation* rel_input   = input_db.get_relation_columnar(fp.name);
    const ColumnarRelation* rel_derived = derived_facts.get_relation_columnar(fp.name);

    if (rel_input && rel_derived) {
        // Both exist — scan each and union.
        auto scan_in  = ScanOp::build(rel_input,   fp, pool, symbol_formids);
        auto scan_de  = ScanOp::build(rel_derived, fp, pool, symbol_formids);
        return std::make_unique<UnionOp>(std::move(scan_in), std::move(scan_de));
    }
    // One or neither exists — ScanOp handles nullptr as no-match.
    return ScanOp::build(rel_input ? rel_input : rel_derived,
                         fp, pool, symbol_formids);
}

// Map VerbKind → action-name prefix (matches Evaluator::verb_prefix).
static const char* verb_prefix(VerbKind v) {
    switch (v) {
        case VerbKind::Set:    return "set_";
        case VerbKind::Add:    return "add_";
        case VerbKind::Sub:    return "sub_";
        case VerbKind::Remove: return "remove_";
    }
    return "set_";
}

// Map FieldOp → the skyrim/{op} effect relation name.
// Returns empty StringId{} if no relation is registered for this op.
static StringId field_op_to_rel(FieldOp op, StringPool& pool) {
    switch (op) {
        case FieldOp::Set:      return pool.intern("skyrim/set");
        case FieldOp::Add:      return pool.intern("skyrim/add");
        case FieldOp::Remove:   return pool.intern("skyrim/remove");
        case FieldOp::Multiply: return pool.intern("skyrim/multiply");
        default:                return StringId{};
    }
}

// Build the body operator tree from a rule's body clauses.
// FactPatterns are sorted by selectivity and joined left-deep.
// GuardClauses are applied AFTER all scans/joins via FilterOp.
// Returns nullopt for unsupported body shapes (Cartesian join between
// any pair of FactPatterns).
static std::optional<std::unique_ptr<Operator>> plan_body(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    // Collect and sort only the FactPattern clauses.
    struct ScoredFP {
        size_t idx;   // index into rule.body
        int    score;
    };
    std::vector<ScoredFP> scored;
    for (size_t i = 0; i < rule.body.size(); ++i) {
        if (auto const* fp = std::get_if<FactPattern>(&rule.body[i].data)) {
            scored.push_back({i, selectivity_score(*fp)});
        }
    }
    // body_is_supported_for_vectorized guarantees at least one FactPattern.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const ScoredFP& a, const ScoredFP& b) {
                         return a.score < b.score;
                     });

    // Build the first (most selective) scan as the initial cumulative op.
    auto const& first_fp = std::get<FactPattern>(rule.body[scored[0].idx].data);
    std::unique_ptr<Operator> cumulative =
        build_source(first_fp, input_db, derived_facts, pool, symbol_formids);
    std::vector<StringId> cumulative_vars = cumulative->output_var_names();

    // Chain subsequent scans via JoinOp.
    for (size_t k = 1; k < scored.size(); ++k) {
        auto const& fp = std::get<FactPattern>(rule.body[scored[k].idx].data);

        auto scan = build_source(fp, input_db, derived_facts, pool, symbol_formids);
        std::vector<StringId> scan_vars = scan->output_var_names();

        // Compute shared vars: intersection of cumulative's output vars and
        // this scan's output vars, preserving cumulative's order.
        std::vector<StringId> shared = var_intersection(cumulative_vars, scan_vars);

        if (shared.empty()) {
            // Cartesian join — reject. Fall back to tuple evaluator.
            return std::nullopt;
        }

        // Update cumulative vars = union(cumulative_vars, scan_vars).
        cumulative_vars = var_union(cumulative_vars, scan_vars);

        // Wrap in a JoinOp.
        cumulative = std::make_unique<JoinOp>(
            std::move(cumulative), std::move(scan), std::move(shared));
    }

    // Walk body in declaration order and splice FilterOps for GuardClauses.
    // Applied AFTER all scans/joins so every variable is bound — correct
    // but doesn't push predicates down. Optimization is Plan 15+.
    for (auto const& clause : rule.body) {
        if (auto const* g = std::get_if<GuardClause>(&clause.data)) {
            cumulative = std::make_unique<FilterOp>(
                std::move(cumulative), g->expr.get(), pool, symbol_formids);
        }
    }

    return cumulative;
}

} // namespace

std::optional<RulePlan> plan_rule(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    // M2: positive FactPattern + optional GuardClause body clauses (at least
    // one FactPattern). Negation, InClause → fallback.
    if (!body_is_supported_for_vectorized(rule)) return std::nullopt;

    // ── Effects branch ───────────────────────────────────────────────
    // Enter if there are any unconditional or conditional effects.
    if (!rule.effects.empty() || !rule.conditional_effects.empty()) {
        std::vector<std::unique_ptr<EffectAppendOp>> effect_ops;
        effect_ops.reserve(rule.effects.size());

        for (const Effect& eff : rule.effects) {
            // Effect must have exactly 2 args: (target, value).
            if (eff.args.size() != 2) return std::nullopt;
            if (!is_simple_arg_expr(eff.args[0]) || !is_simple_arg_expr(eff.args[1]))
                return std::nullopt;

            // Reconstruct the legacy action name: "<verb>_<eff.name>".
            std::string const legacy =
                std::string(verb_prefix(eff.verb)) + std::string(pool.get(eff.name));
            StringId const action_id = pool.intern(legacy);

            // Map action name → (FieldId, FieldOp).
            auto [field, op] = mora::action_to_field(action_id, pool);
            if (field == FieldId::Invalid) return std::nullopt;  // unknown action

            // Map FieldOp → output relation name.
            StringId const out_rel = field_op_to_rel(op, pool);
            if (out_rel.index == 0) return std::nullopt;  // unsupported op

            // Re-plan the body for this effect (re-scan strategy).
            auto body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
            if (!body) return std::nullopt;

            StringId const field_kw = pool.intern(field_id_name(field));

            EffectArgSpec target_spec =
                spec_from_expr(eff.args[0], pool, symbol_formids);
            EffectArgSpec value_spec  =
                spec_from_expr(eff.args[1], pool, symbol_formids);

            effect_ops.push_back(std::make_unique<EffectAppendOp>(
                std::move(*body), out_rel, field_kw,
                std::move(target_spec), std::move(value_spec)));
        }

        // ConditionalEffect: same as unconditional, but body is wrapped in
        // a FilterOp keyed on the conditional's guard expression.
        for (const ConditionalEffect& ce : rule.conditional_effects) {
            if (ce.effect.args.size() != 2) return std::nullopt;
            if (!is_simple_arg_expr(ce.effect.args[0]) || !is_simple_arg_expr(ce.effect.args[1]))
                return std::nullopt;

            std::string const ce_legacy =
                std::string(verb_prefix(ce.effect.verb)) + std::string(pool.get(ce.effect.name));
            StringId const ce_action_id = pool.intern(ce_legacy);

            auto [ce_field, ce_op] = mora::action_to_field(ce_action_id, pool);
            if (ce_field == FieldId::Invalid) return std::nullopt;

            StringId const ce_out_rel = field_op_to_rel(ce_op, pool);
            if (ce_out_rel.index == 0) return std::nullopt;

            auto ce_body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
            if (!ce_body) return std::nullopt;

            // Wrap the body in a FilterOp for this conditional's guard.
            auto filtered = std::make_unique<FilterOp>(
                std::move(*ce_body), ce.guard.get(), pool, symbol_formids);

            StringId const ce_field_kw = pool.intern(field_id_name(ce_field));

            effect_ops.push_back(std::make_unique<EffectAppendOp>(
                std::move(filtered), ce_out_rel, ce_field_kw,
                spec_from_expr(ce.effect.args[0], pool, symbol_formids),
                spec_from_expr(ce.effect.args[1], pool, symbol_formids)));
        }

        RulePlan plan;
        plan.effect_ops = std::move(effect_ops);
        return plan;
    }

    // ── Derived rule branch ───────────────────────────────────────────
    // No effects and no conditional effects: derived fact rule.
    {
        auto body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
        if (!body) return std::nullopt;

        // All head args must be VariableExprs.
        std::vector<EffectArgSpec> head_specs;
        head_specs.reserve(rule.head_args.size());
        for (auto const& ha : rule.head_args) {
            if (!std::holds_alternative<VariableExpr>(ha.data))
                return std::nullopt;
            head_specs.push_back(spec_from_expr(ha, pool, symbol_formids));
        }

        RulePlan plan;
        plan.derived_op = std::make_unique<DerivedAppendOp>(
            std::move(*body), rule.name, std::move(head_specs));
        return plan;
    }
}

} // namespace mora
