#pragma once
#include "mora/ast/ast.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/field_types.h"
#include "mora/eval/phase_classifier.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <functional>
#include <unordered_map>

namespace mora {

using ProgressCallback = std::function<void(size_t current, size_t total, std::string_view rule_name)>;

using Bindings = std::unordered_map<uint32_t, Value>; // key: StringId.index

class Evaluator {
public:
    Evaluator(StringPool& pool, DiagBag& diags, const FactDB& db);

    void set_symbol_formid(StringId symbol_name, uint32_t formid);

    // Evaluate all rules and write effect facts directly into out_facts.
    // Optional callback fires after each rule for progress reporting.
    void evaluate_module(const Module& mod, FactDB& out_facts,
                         ProgressCallback progress = nullptr);

    // Returns the number of rules that went through the vectorized path
    // (as opposed to the tuple-based match_clauses fallback). Useful for
    // verifying coverage in tests. Removed once the fallback is deleted.
    size_t vectorized_rules_count() const { return vectorized_rules_count_; }

private:
    void evaluate_rule(const Rule& rule, FactDB& db);
    static std::vector<size_t> compute_clause_order(const Rule& rule) ;
    void match_clauses(const Rule& rule, const std::vector<size_t>& order,
                       size_t step, Bindings& bindings, FactDB& db);
    std::vector<Bindings> match_fact_pattern(const FactPattern& pattern,
                                              const Bindings& bindings);
    bool evaluate_guard(const Expr& expr, const Bindings& bindings);
    void apply_effects(const Rule& rule, const Bindings& bindings, FactDB& db);
    Value resolve_expr(const Expr& expr, const Bindings& bindings);
    std::pair<FieldId, FieldOp> action_to_field(StringId action) const;

    std::vector<Tuple> merged_query(StringId relation, const Tuple& pattern);

    void ensure_effect_relations_configured(FactDB& db);

    StringPool& pool_;
    DiagBag& diags_;
    const FactDB& db_;
    FactDB derived_facts_;
    std::unordered_map<uint32_t, uint32_t> symbol_formids_;

    StringId effect_rel_set_;
    StringId effect_rel_add_;
    StringId effect_rel_remove_;
    StringId effect_rel_multiply_;
    bool     effect_rels_configured_ = false;

    // Side-channel counter: how many rules ran via the vectorized path.
    size_t vectorized_rules_count_ = 0;
};

} // namespace mora
