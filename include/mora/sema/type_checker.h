#pragma once
#include "mora/ast/ast.h"
#include "mora/sema/name_resolver.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <unordered_map>
#include <unordered_set>

namespace mora {

class TypeChecker {
public:
    TypeChecker(StringPool& pool, DiagBag& diags, const NameResolver& resolver);
    void check(Module& mod);

private:
    void check_rule(Rule& rule);
    void check_fact_pattern(const FactPattern& pattern);
    void check_effect(const Effect& effect);
    void check_guard(const Expr& expr);
    MoraType infer_expr_type(const Expr& expr);

    void bind_variable(StringId name, MoraType type, const SourceSpan& span);
    MoraType lookup_variable(StringId name) const;
    void check_unused_variables(const Rule& rule);

    StringPool& pool_;
    DiagBag& diags_;
    const NameResolver& resolver_;

    // Per-rule state (reset for each rule)
    std::unordered_map<uint32_t, MoraType> var_types_;
    std::unordered_set<uint32_t> var_used_;
    std::unordered_map<uint32_t, SourceSpan> var_def_spans_;
};

} // namespace mora
