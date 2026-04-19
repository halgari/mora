#include "mora/eval/evaluator.h"
#include "mora/eval/rule_planner.h"
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

void Evaluator::evaluate_module(const Module& mod, FactDB& out_facts,
                                  ProgressCallback progress) {
    current_module_ = &mod;
    for (size_t i = 0; i < mod.rules.size(); ++i) {
        const Rule& rule = mod.rules[i];
        evaluate_rule(rule, out_facts);
        if (progress) progress(i + 1, mod.rules.size(), pool_.get(rule.name));
    }
    current_module_ = nullptr;
}

void Evaluator::evaluate_rule(const Rule& rule, FactDB& db) {
    auto plan = plan_rule(rule, db_, derived_facts_, pool_, symbol_formids_);
    if (!plan) {
        std::string const src_line = current_module_
            ? current_module_->get_line(rule.span.start_line)
            : std::string{};
        diags_.error("eval-unsupported",
                      std::string("internal: vectorized planner declined rule '") +
                          std::string(pool_.get(rule.name)) + "'",
                      rule.span, src_line);
        return;
    }
    // Skyrim output relations (qualifier == "skyrim") write directly to db.
    // User-defined rules (unqualified or other qualifier) write to derived_facts_.
    FactDB& target = (rule.qualifier.index != 0 &&
                      pool_.get(rule.qualifier) == "skyrim")
                     ? db
                     : derived_facts_;
    plan->append_op->run(target);
}

} // namespace mora
