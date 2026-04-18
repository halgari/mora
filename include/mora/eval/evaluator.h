#pragma once
#include "mora/ast/ast.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/phase_classifier.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <functional>
#include <unordered_map>

namespace mora {

using ProgressCallback = std::function<void(size_t current, size_t total, std::string_view rule_name)>;

class Evaluator {
public:
    Evaluator(StringPool& pool, DiagBag& diags, const FactDB& db);

    void set_symbol_formid(StringId symbol_name, uint32_t formid);

    // Evaluate all rules and write effect facts directly into out_facts.
    // Optional callback fires after each rule for progress reporting.
    void evaluate_module(const Module& mod, FactDB& out_facts,
                         ProgressCallback progress = nullptr);

private:
    void evaluate_rule(const Rule& rule, FactDB& db);
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

    // Only populated during evaluate_module; read by evaluate_rule when
    // emitting diagnostics so the renderer has a source line to show.
    const Module* current_module_ = nullptr;
};

} // namespace mora
