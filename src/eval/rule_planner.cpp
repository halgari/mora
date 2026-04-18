#include "mora/eval/rule_planner.h"

#include "mora/data/action_names.h"
#include "mora/eval/expr_eval.h"
#include "mora/eval/field_types.h"
#include "mora/eval/op_antijoin.h"
#include "mora/eval/op_filter.h"
#include "mora/eval/op_in_clause.h"
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
        || std::holds_alternative<SymbolExpr>(e.data)
        || std::holds_alternative<EditorIdExpr>(e.data);
}

// Returns true iff every body clause is either:
// - a positive (non-negated) FactPattern with only simple args, or
// - a negated FactPattern with only simple args (M3: AntiJoinOp), or
// - a GuardClause (M2: guards are supported via FilterOp), or
// - an InClause with a single values expression (M3: InClauseOp).
// OrClause → false (not supported).
bool body_is_supported_for_vectorized(const Rule& rule) {
    // Must have at least one positive FactPattern to scan.
    bool has_positive_fact_pattern = false;
    for (auto const& clause : rule.body) {
        bool ok = std::visit([](auto const& c) -> bool {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) {
                return true;  // both positive and negated are now supported
            }
            if constexpr (std::is_same_v<T, GuardClause>) {
                return true;  // M2: guards are supported
            }
            if constexpr (std::is_same_v<T, InClause>) {
                // M3: single-value InClause (list/generator or membership).
                // P15 M1: multi-value literal sets (values.size() > 1) are
                // now also accepted — handled by build_membership_set in
                // plan_body.  Zero-value InClause always produces no rows
                // (empty membership set), still accepted.
                return true;
            }
            return false;  // OrClause → reject
        }, clause.data);
        if (!ok) return false;

        // Verify all FactPattern args are simple.
        if (auto const* fp = std::get_if<FactPattern>(&clause.data)) {
            for (auto const& a : fp->args) {
                if (!is_simple_arg_expr(a)) return false;
            }
            if (!fp->negated) has_positive_fact_pattern = true;
        }
    }
    return has_positive_fact_pattern;
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
// For arbitrary expression kinds (BinaryExpr, CallExpr, etc.) that cannot be
// resolved at plan time, returns Kind::Expr with a pointer to the Expr for
// per-row evaluation via resolve_expr.
EffectArgSpec spec_from_expr(const Expr& e,
                              StringPool& pool,
                              const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    (void)pool;
    EffectArgSpec s{};
    if (auto const* ve = std::get_if<VariableExpr>(&e.data)) {
        s.kind     = EffectArgSpec::Kind::Var;
        s.var_name = ve->name;
        return s;
    }
    if (auto const* il = std::get_if<IntLiteral>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        s.constant = Value::make_int(il->value);
        return s;
    }
    if (auto const* fl = std::get_if<FloatLiteral>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        s.constant = Value::make_float(fl->value);
        return s;
    }
    if (auto const* sl = std::get_if<StringLiteral>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        s.constant = Value::make_string(sl->value);
        return s;
    }
    if (auto const* kl = std::get_if<KeywordLiteral>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        auto it = symbols.find(kl->value.index);
        s.constant = (it != symbols.end())
            ? Value::make_formid(it->second)
            : Value::make_keyword(kl->value);
        return s;
    }
    if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        auto it = symbols.find(se->name.index);
        s.constant = (it != symbols.end())
            ? Value::make_formid(it->second)
            : Value::make_var();
        return s;
    }
    if (auto const* eid = std::get_if<EditorIdExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        auto it = symbols.find(eid->name.index);
        s.constant = (it != symbols.end())
            ? Value::make_formid(it->second)
            : Value::make_var();
        return s;
    }
    // Anything else (BinaryExpr, CallExpr, FieldAccessExpr): evaluate per-row.
    s.kind = EffectArgSpec::Kind::Expr;
    s.expr = &e;
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
// Positive FactPatterns are sorted by selectivity and joined left-deep.
// Negated FactPatterns (M3) are appended as AntiJoinOps after positive joins.
// InClauses (M3) are spliced in body order after all positive joins.
// GuardClauses (M2) are applied AFTER everything else via FilterOp.
// Returns nullopt for unsupported body shapes (Cartesian join, anti-Cartesian
// negation, or InClause-first with unbound var).
static std::optional<std::unique_ptr<Operator>> plan_body(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    // Collect and sort only the POSITIVE FactPattern clauses.
    struct ScoredFP {
        size_t idx;   // index into rule.body
        int    score;
    };
    std::vector<ScoredFP> scored;
    for (size_t i = 0; i < rule.body.size(); ++i) {
        if (auto const* fp = std::get_if<FactPattern>(&rule.body[i].data)) {
            if (!fp->negated) {
                scored.push_back({i, selectivity_score(*fp)});
            }
        }
    }
    // body_is_supported_for_vectorized guarantees at least one positive FactPattern.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const ScoredFP& a, const ScoredFP& b) {
                         return a.score < b.score;
                     });

    // Build the first (most selective) scan as the initial cumulative op.
    auto const& first_fp = std::get<FactPattern>(rule.body[scored[0].idx].data);
    std::unique_ptr<Operator> cumulative =
        build_source(first_fp, input_db, derived_facts, pool, symbol_formids);
    std::vector<StringId> cumulative_vars = cumulative->output_var_names();

    // Chain subsequent positive scans via JoinOp.
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

    // M3: Walk body in declaration order and splice AntiJoinOps for negated
    // FactPatterns and InClauseOps for InClauses.
    //
    // Both are applied AFTER all positive scans/joins so that the maximum set
    // of variables is bound — correct but does not push operators down.
    // Optimization is Plan 15+.
    for (auto const& clause : rule.body) {
        if (auto const* fp = std::get_if<FactPattern>(&clause.data)) {
            if (!fp->negated) continue;

            // Negated FactPattern: build a scan for the right (probe) side.
            auto neg_source =
                build_source(*fp, input_db, derived_facts, pool, symbol_formids);
            std::vector<StringId> neg_vars = neg_source->output_var_names();

            // Shared vars = intersection of cumulative output and negated scan output.
            std::vector<StringId> shared =
                var_intersection(cumulative_vars, neg_vars);

            if (shared.empty()) {
                // Anti-Cartesian — planner cannot express this. Fall back.
                return std::nullopt;
            }

            cumulative = std::make_unique<AntiJoinOp>(
                std::move(cumulative), std::move(neg_source), std::move(shared));
            // cumulative_vars unchanged — right columns don't appear in output.

        } else if (auto const* ic = std::get_if<InClause>(&clause.data)) {
            // InClause: determine generator vs membership based on whether
            // the variable is already bound in cumulative_vars.
            const auto* ve = std::get_if<VariableExpr>(&ic->variable->data);
            if (!ve) {
                // Non-variable LHS — not supported. Fall back.
                return std::nullopt;
            }

            // Check if ?var is already bound.
            bool is_bound = false;
            for (auto const& cv : cumulative_vars) {
                if (cv.index == ve->name.index) { is_bound = true; break; }
            }

            if (ic->values.empty()) {
                // Empty set — always false. Use MembershipSet with empty list.
                cumulative = InClauseOp::build_membership_set(
                    std::move(cumulative), ve->name, {},
                    pool, symbol_formids);
            } else if (ic->values.size() == 1 && is_bound) {
                // Single-expression membership (e.g. ?x in SomeList where
                // SomeList is a bound variable holding a List value).
                cumulative = InClauseOp::build_membership(
                    std::move(cumulative), ve->name, &ic->values[0],
                    pool, symbol_formids);
            } else if (ic->values.size() == 1 && !is_bound) {
                // Single-expression generator: iterates the list and binds the var.
                cumulative = InClauseOp::build_generator(
                    std::move(cumulative), ve->name, &ic->values[0],
                    pool, symbol_formids);
                cumulative_vars.push_back(ve->name);
            } else {
                // Multi-value literal set: `?x in [:A, :B, ...]`.
                // Always a membership check (values are literals, not a List).
                std::vector<const Expr*> exprs;
                exprs.reserve(ic->values.size());
                for (auto const& v : ic->values) exprs.push_back(&v);
                cumulative = InClauseOp::build_membership_set(
                    std::move(cumulative), ve->name, std::move(exprs),
                    pool, symbol_formids);
                // If var was unbound (values.size() > 1), it's now bound as a
                // filter (the set enumerates constants, not list elements).
                if (!is_bound) cumulative_vars.push_back(ve->name);
            }
        }
    }

    // Walk body in declaration order and splice FilterOps for GuardClauses.
    // Applied last so every variable is bound — correct but doesn't push
    // predicates down. Optimization is Plan 15+.
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
            // (Leveled-list add uses 4 args — not yet vectorized; fall back.)
            if (eff.args.size() != 2) return std::nullopt;

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
                std::move(target_spec), std::move(value_spec),
                pool, symbol_formids));
        }

        // ConditionalEffect: same as unconditional, but body is wrapped in
        // a FilterOp keyed on the conditional's guard expression.
        for (const ConditionalEffect& ce : rule.conditional_effects) {
            if (ce.effect.args.size() != 2) return std::nullopt;

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
                spec_from_expr(ce.effect.args[1], pool, symbol_formids),
                pool, symbol_formids));
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
            std::move(*body), rule.name, std::move(head_specs),
            pool, symbol_formids);
        return plan;
    }
}

} // namespace mora
