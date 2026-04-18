#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/op_append.h"
#include "mora/eval/operator.h"

#include <memory>
#include <optional>
#include <unordered_map>

namespace mora {

// Outcome of attempting to build a vectorized plan for a rule.
// Either `effect_ops` (non-empty) or `derived_op` is populated (never both).
// Each effect gets its own fresh operator tree (re-scan strategy).
struct RulePlan {
    std::vector<std::unique_ptr<EffectAppendOp>> effect_ops;
    std::unique_ptr<DerivedAppendOp>             derived_op;
};

// Returns RulePlan if the rule is supported in MVP; otherwise nullopt
// (caller falls back to tuple evaluator).
std::optional<RulePlan> plan_rule(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids);

} // namespace mora
