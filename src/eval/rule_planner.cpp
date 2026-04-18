#include "mora/eval/rule_planner.h"

#include "mora/data/action_names.h"
#include "mora/eval/field_types.h"
#include "mora/eval/op_join.h"
#include "mora/eval/op_scan.h"
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

// Returns true iff every body clause is a positive (non-negated) FactPattern
// with only simple args. Guards, InClause, negated patterns → false.
bool body_is_positive_conjunction(const Rule& rule) {
    if (rule.body.empty()) return false;
    for (auto const& clause : rule.body) {
        bool ok = std::visit([](auto const& c) -> bool {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) {
                return !c.negated;
            }
            return false;  // GuardClause, InClause → reject
        }, clause.data);
        if (!ok) return false;

        // Also verify all args are simple.
        if (auto const* fp = std::get_if<FactPattern>(&clause.data)) {
            for (auto const& a : fp->args) {
                if (!is_simple_arg_expr(a)) return false;
            }
        }
    }
    return true;
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

// Build an EffectArgSpec from an Expr.
EffectArgSpec spec_from_expr(const Expr& e,
                              const std::unordered_map<uint32_t, uint32_t>& symbols)
{
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
        s.constant = Value::make_keyword(kl->value);
    } else if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        auto it = symbols.find(se->name.index);
        s.constant = (it == symbols.end())
            ? Value::make_var()  // unresolved — EffectAppendOp will skip non-FormID targets
            : Value::make_formid(it->second);
    }
    return s;
}

} // namespace

std::optional<RulePlan> plan_rule(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    // M2: any number of positive FactPattern body clauses (at least one).
    // Guards, negation, InClause, zero clauses → fallback.
    if (!body_is_positive_conjunction(rule)) return std::nullopt;

    // Conditional effects force fallback.
    if (!rule.conditional_effects.empty()) return std::nullopt;

    // ── Collect and sort FactPatterns by selectivity ──────────────────
    // body_is_positive_conjunction already verified all clauses are
    // non-negated FactPatterns with simple args.
    struct ScoredFP {
        size_t     idx;   // original index in rule.body
        int        score;
    };
    std::vector<ScoredFP> scored;
    scored.reserve(rule.body.size());
    for (size_t i = 0; i < rule.body.size(); ++i) {
        auto const& fp = std::get<FactPattern>(rule.body[i].data);
        scored.push_back({i, selectivity_score(fp)});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const ScoredFP& a, const ScoredFP& b) {
                         return a.score < b.score;
                     });

    // ── Build ScanOps in selectivity order ───────────────────────────
    // `cumulative` is the current left-deep tree; `cumulative_vars` tracks
    // all variable names output by `cumulative` so far.

    auto lookup_rel = [&](StringId rel_name) -> const ColumnarRelation* {
        const ColumnarRelation* rel = input_db.get_relation_columnar(rel_name);
        if (rel == nullptr) rel = derived_facts.get_relation_columnar(rel_name);
        return rel;  // may be null — ScanOp handles that as no_match
    };

    // Build the first (most selective) scan as the initial cumulative op.
    auto const& first_fp = std::get<FactPattern>(rule.body[scored[0].idx].data);
    std::unique_ptr<Operator> cumulative =
        ScanOp::build(lookup_rel(first_fp.name), first_fp, pool, symbol_formids);
    std::vector<StringId> cumulative_vars = cumulative->output_var_names();

    // Chain subsequent scans via JoinOp.
    for (size_t k = 1; k < scored.size(); ++k) {
        auto const& fp = std::get<FactPattern>(rule.body[scored[k].idx].data);

        auto scan = ScanOp::build(lookup_rel(fp.name), fp, pool, symbol_formids);
        std::vector<StringId> scan_vars = scan->output_var_names();

        // Compute shared vars: intersection of cumulative's output vars and
        // this scan's output vars, preserving cumulative's order.
        std::vector<StringId> shared = var_intersection(cumulative_vars, scan_vars);

        if (shared.empty()) {
            // Cartesian join — reject. Fall back to tuple evaluator.
            // (A Cartesian join produces O(N*M) rows and is usually a bug
            // in the rule; Plan 14 can handle this specially if needed.)
            return std::nullopt;
        }

        // Update cumulative vars = union(cumulative_vars, scan_vars).
        cumulative_vars = var_union(cumulative_vars, scan_vars);

        // Wrap in a JoinOp.
        cumulative = std::make_unique<JoinOp>(
            std::move(cumulative), std::move(scan), std::move(shared));
    }

    // ── Effects branch ───────────────────────────────────────────────
    if (!rule.effects.empty()) {
        // MVP: exactly one effect. Multiple effects → fallback.
        if (rule.effects.size() > 1) return std::nullopt;

        const Effect& eff = rule.effects[0];

        // Only Set verbs for now (Add/Sub/Remove/Mul → Plan 14).
        if (eff.verb != VerbKind::Set) return std::nullopt;

        // Effect must have exactly 2 args: (target, value).
        if (eff.args.size() != 2) return std::nullopt;
        if (!is_simple_arg_expr(eff.args[0]) || !is_simple_arg_expr(eff.args[1]))
            return std::nullopt;

        // Reconstruct the legacy action name: "set_<eff.name>".
        std::string const legacy = "set_" + std::string(pool.get(eff.name));
        StringId const action_id = pool.intern(legacy);

        // Map action name → (FieldId, FieldOp).
        auto [field, op] = mora::action_to_field(action_id, pool);
        if (field == FieldId::Invalid) return std::nullopt;  // unknown action

        // Only FieldOp::Set (action_to_field may return Add/Multiply for
        // unusual "set_"-prefixed names in edge cases — guard explicitly).
        if (op != FieldOp::Set) return std::nullopt;

        StringId const out_rel  = pool.intern("skyrim/set");
        StringId const field_kw = pool.intern(field_id_name(field));

        EffectArgSpec target_spec = spec_from_expr(eff.args[0], symbol_formids);
        EffectArgSpec value_spec  = spec_from_expr(eff.args[1], symbol_formids);

        RulePlan plan;
        plan.effect_op = std::make_unique<EffectAppendOp>(
            std::move(cumulative), out_rel, field_kw,
            std::move(target_spec), std::move(value_spec));
        return plan;
    }

    // ── Derived rule branch ───────────────────────────────────────────
    // No effects and no conditional effects: derived fact rule.
    // All head args must be VariableExprs.
    {
        std::vector<EffectArgSpec> head_specs;
        head_specs.reserve(rule.head_args.size());
        for (auto const& ha : rule.head_args) {
            if (!std::holds_alternative<VariableExpr>(ha.data))
                return std::nullopt;
            head_specs.push_back(spec_from_expr(ha, symbol_formids));
        }
        RulePlan plan;
        plan.derived_op = std::make_unique<DerivedAppendOp>(
            std::move(cumulative), rule.name, std::move(head_specs));
        return plan;
    }
}

} // namespace mora
