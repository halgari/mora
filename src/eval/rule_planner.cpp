#include "mora/eval/rule_planner.h"

#include "mora/data/action_names.h"
#include "mora/eval/field_types.h"
#include "mora/eval/op_scan.h"
#include "mora/model/field_names.h"

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

bool body_is_single_positive_fact_pattern(const Rule& rule) {
    if (rule.body.size() != 1) return false;
    return std::visit([](auto const& c) -> bool {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, FactPattern>) {
            return !c.negated;
        }
        return false;
    }, rule.body[0].data);
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
    // M1 restriction: exactly one body clause, which must be a positive
    // FactPattern. Guards, negation, InClause, multiple clauses → fallback.
    if (!body_is_single_positive_fact_pattern(rule)) return std::nullopt;

    auto const& fp = std::get<FactPattern>(rule.body[0].data);

    // All pattern args must be "simple" (var/literal/symbol).
    for (auto const& a : fp.args) {
        if (!is_simple_arg_expr(a)) return std::nullopt;
    }

    // Conditional effects force fallback.
    if (!rule.conditional_effects.empty()) return std::nullopt;

    // Find the source relation — check input_db first, then derived_facts.
    StringId const rel_name = fp.name;
    const ColumnarRelation* rel = input_db.get_relation_columnar(rel_name);
    if (rel == nullptr) {
        rel = derived_facts.get_relation_columnar(rel_name);
    }

    // Build the ScanOp (rel may be null — ScanOp handles it as no_match).
    auto scan = ScanOp::build(rel, fp, pool, symbol_formids);

    // ── Effects branch ──────────────────────────────────────────────────
    if (!rule.effects.empty()) {
        // MVP: exactly one effect. Multiple effects → fallback.
        if (rule.effects.size() > 1) return std::nullopt;

        const Effect& eff = rule.effects[0];

        // Only Set verbs in M1 (Add/Sub/Remove/Mul require separate output
        // relation routing — Plan 14 expands this).
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
        if (field == FieldId::Invalid) return std::nullopt;   // unknown action

        // Only FieldOp::Set for M1 (action_to_field may return Add/Multiply
        // for "set_" prefixed names in edge cases — guard explicitly).
        if (op != FieldOp::Set) return std::nullopt;

        StringId const out_rel    = pool.intern("skyrim/set");
        StringId const field_kw   = pool.intern(field_id_name(field));

        EffectArgSpec target_spec = spec_from_expr(eff.args[0], symbol_formids);
        EffectArgSpec value_spec  = spec_from_expr(eff.args[1], symbol_formids);

        RulePlan plan;
        plan.effect_op = std::make_unique<EffectAppendOp>(
            std::move(scan), out_rel, field_kw,
            std::move(target_spec), std::move(value_spec));
        return plan;
    }

    // ── Derived rule branch ────────────────────────────────────────────
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
            std::move(scan), rule.name, std::move(head_specs));
        return plan;
    }
}

} // namespace mora
