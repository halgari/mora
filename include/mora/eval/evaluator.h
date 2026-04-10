#pragma once
#include "mora/ast/ast.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/eval/phase_classifier.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>

namespace mora {

using Bindings = std::unordered_map<uint32_t, Value>; // key: StringId.index

class Evaluator {
public:
    Evaluator(StringPool& pool, DiagBag& diags, const FactDB& db);

    void set_symbol_formid(StringId symbol_name, uint32_t formid);

    // Evaluate all static rules, return patches
    PatchSet evaluate_static(const Module& mod);

private:
    void evaluate_rule(const Rule& rule, PatchSet& patches, uint32_t priority);
    void match_clauses(const Rule& rule, size_t clause_idx,
                       Bindings& bindings, PatchSet& patches, uint32_t priority);
    std::vector<Bindings> match_fact_pattern(const FactPattern& pattern,
                                              const Bindings& bindings);
    bool evaluate_guard(const Expr& expr, const Bindings& bindings);
    void apply_effects(const Rule& rule, const Bindings& bindings,
                       PatchSet& patches, uint32_t priority);
    Value resolve_expr(const Expr& expr, const Bindings& bindings);
    std::pair<FieldId, FieldOp> action_to_field(StringId action) const;

    StringPool& pool_;
    DiagBag& diags_;
    const FactDB& db_;
    FactDB derived_facts_;
    std::unordered_map<uint32_t, uint32_t> symbol_formids_;
    StringId current_mod_name_;
};

} // namespace mora
