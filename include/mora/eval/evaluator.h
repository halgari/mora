#pragma once
#include "mora/ast/ast.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
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

    // Evaluate all static rules, return patches.
    // Optional callback fires after each rule for progress reporting.
    PatchSet evaluate_static(const Module& mod,
                             ProgressCallback progress = nullptr);

private:
    void evaluate_rule(const Rule& rule, PatchSet& patches, uint32_t priority);
    std::vector<size_t> compute_clause_order(const Rule& rule) const;
    void match_clauses(const Rule& rule, const std::vector<size_t>& order,
                       size_t step, Bindings& bindings, PatchSet& patches,
                       uint32_t priority);
    std::vector<Bindings> match_fact_pattern(const FactPattern& pattern,
                                              const Bindings& bindings);
    bool evaluate_guard(const Expr& expr, const Bindings& bindings);
    void apply_effects(const Rule& rule, const Bindings& bindings,
                       PatchSet& patches, uint32_t priority);
    Value resolve_expr(const Expr& expr, const Bindings& bindings);
    std::pair<FieldId, FieldOp> action_to_field(StringId action) const;

    // Cached query results: key = hash of (relation StringId + query tuple),
    // value = index into query_cache_results_.
    struct CacheKey {
        uint32_t relation;
        Tuple pattern;
        bool operator==(const CacheKey& o) const {
            if (relation != o.relation || pattern.size() != o.pattern.size()) return false;
            for (size_t i = 0; i < pattern.size(); i++) {
                if (!(pattern[i] == o.pattern[i])) return false;
            }
            return true;
        }
    };
    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const {
            size_t h = std::hash<uint32_t>{}(k.relation);
            for (auto& v : k.pattern) h ^= v.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<CacheKey, std::vector<Tuple>, CacheKeyHash> query_cache_;

    const std::vector<Tuple>& cached_query(StringId relation, const Tuple& pattern);

    StringPool& pool_;
    DiagBag& diags_;
    const FactDB& db_;
    FactDB derived_facts_;
    std::unordered_map<uint32_t, uint32_t> symbol_formids_;
    StringId current_mod_name_;
};

} // namespace mora
