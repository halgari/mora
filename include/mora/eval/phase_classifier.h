#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mora {

// Legacy binary phase. Kept for existing callers.
enum class Phase { Static, Dynamic };

// v2 three-tier phase classification.
enum class PhaseClass : uint8_t { Static, Maintain, On };

struct RuleClassification {
    StringId rule_name;
    Phase phase;
    std::string reason;
};

class PhaseClassifier {
public:
    // Legacy constructor.
    explicit PhaseClassifier(StringPool& pool);
    // v2 constructor with diagnostics.
    PhaseClassifier(StringPool& pool, DiagBag& diag);

    // Legacy API.
    Phase classify(const Rule& rule) const;
    std::vector<RuleClassification> classify_module(const Module& mod) const;

    // v2 API: three-tier classification keyed by rule index.
    std::unordered_map<size_t, PhaseClass> classify(const Module& mod);

private:
    bool is_instance_fact(StringId name) const;
    bool body_has_instance_facts(const Rule& rule) const;
    std::unordered_set<uint32_t> instance_facts_;
    StringPool* pool_ = nullptr;
    DiagBag* diag_ = nullptr;
};

} // namespace mora
