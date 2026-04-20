#include "mora/eval/rule_planner.h"

#include "mora/eval/expr_eval.h"
#include "mora/eval/op_antijoin.h"
#include "mora/eval/op_filter.h"
#include "mora/eval/op_in_clause.h"
#include "mora/eval/op_join.h"
#include "mora/eval/op_scan.h"
#include "mora/eval/op_union.h"

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
        || std::holds_alternative<EditorIdExpr>(e.data)
        || std::holds_alternative<FormIdLiteral>(e.data);
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
// symbol_formids. For arbitrary expression kinds (BinaryExpr, CallExpr, etc.)
// that cannot be resolved at plan time, returns Kind::Expr with a pointer to
// the Expr for per-row evaluation via resolve_expr.
// Case-insensitive lookup in symbols_ — Skyrim treats EditorIDs as
// case-insensitive, and main.cpp's ESP-load → set_symbol_formid loop
// feeds editor_ids_ (which lowercases keys) into the evaluator. The
// original-case StringId in the AST won't match the lowercased entry;
// fall back by lowercasing + re-interning. Mirrors the equivalent
// branch in expr_eval.cpp's resolve_expr.
static uint32_t lookup_symbol_ci(
    StringId name,
    StringPool& pool,
    const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    auto it = symbols.find(name.index);
    if (it != symbols.end()) return it->second;
    std::string lower(pool.get(name));
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    it = symbols.find(pool.intern(lower).index);
    return (it == symbols.end()) ? 0u : it->second;
}

EffectArgSpec spec_from_expr(const Expr& e,
                              StringPool& pool,
                              const std::unordered_map<uint32_t, uint32_t>& symbols)
{
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
        uint32_t fid = lookup_symbol_ci(kl->value, pool, symbols);
        s.constant = (fid != 0)
            ? Value::make_formid(fid)
            : Value::make_keyword(kl->value);
        return s;
    }
    if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        uint32_t fid = lookup_symbol_ci(se->name, pool, symbols);
        s.constant = (fid != 0)
            ? Value::make_formid(fid)
            : Value::make_var();
        return s;
    }
    if (auto const* eid = std::get_if<EditorIdExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        uint32_t fid = lookup_symbol_ci(eid->name, pool, symbols);
        s.constant = (fid != 0)
            ? Value::make_formid(fid)
            : Value::make_var();
        return s;
    }
    if (auto const* fl = std::get_if<FormIdLiteral>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        s.constant = Value::make_formid(fl->value);
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

// Build the body operator tree from a rule's body clauses.
// Positive FactPatterns are sorted by selectivity and joined left-deep.
// Negated FactPatterns (M3) are appended as AntiJoinOps after positive joins.
// InClauses (M3) are spliced in body order after all positive joins.
// GuardClauses (M2) are applied AFTER everything else via FilterOp.
// Returns nullopt for unsupported body shapes (Cartesian join,
// anti-Cartesian negation, or non-variable InClause LHS); the caller
// emits a hard diagnostic.
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
            // Cartesian join — reject; caller emits a diagnostic.
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
                // Anti-Cartesian — planner cannot express this.
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
                // Non-variable LHS — not supported.
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
    if (!body_is_supported_for_vectorized(rule)) return std::nullopt;

    auto body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
    if (!body) return std::nullopt;

    // Build head arg specs. For skyrim/* qualified rules (effect relations), the
    // head may include keyword literals (:Keyword, :Name, etc.) as well as
    // variables and other expressions. For user-defined rules, all head args
    // must be simple variable expressions.
    std::vector<EffectArgSpec> head_specs;
    head_specs.reserve(rule.head_args.size());
    for (const Expr& ha : rule.head_args) {
        if (rule.qualifier.index == 0) {
            // User-defined rule: head args must be variables.
            if (!std::holds_alternative<VariableExpr>(ha.data))
                return std::nullopt;
        }
        head_specs.push_back(spec_from_expr(ha, pool, symbol_formids));
    }

    // Determine the target relation name. Qualified → "ns/name"; else just "name".
    StringId head_rel;
    if (rule.qualifier.index != 0) {
        std::string qn;
        qn += pool.get(rule.qualifier);
        qn += '/';
        qn += pool.get(rule.name);
        head_rel = pool.intern(qn);
    } else {
        head_rel = rule.name;
    }

    RulePlan plan;
    plan.append_op = std::make_unique<DerivedAppendOp>(
        std::move(*body), head_rel, std::move(head_specs),
        pool, symbol_formids);
    return plan;
}

} // namespace mora
