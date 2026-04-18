#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <cstddef>
#include <unordered_map>

namespace mora {

struct FactSignature {
    StringId name;
    size_t   arity = 0;
    bool     is_builtin = false;
};

class NameResolver {
public:
    NameResolver(StringPool& pool, DiagBag& diags);
    void resolve(Module& mod);
    const FactSignature* lookup_fact(StringId name) const;

    // Read-only views of registered facts/rules. Valid after resolve() has run.
    const std::unordered_map<uint32_t, FactSignature>& facts() const { return facts_; }
    const std::unordered_map<uint32_t, bool>& rules() const { return rules_; }

private:
    void register_builtins();
    void register_rule_as_fact(const Rule& rule);
    void resolve_rule(Rule& rule);
    void resolve_clause(Clause& clause);
    void check_fact_exists(const FactPattern& pattern);
    std::string source_line(const SourceSpan& span) const;

    StringPool& pool_;
    DiagBag& diags_;
    const Module* current_mod_ = nullptr;
    std::unordered_map<uint32_t, FactSignature> facts_;
    std::unordered_map<uint32_t, bool> rules_;
};

} // namespace mora
