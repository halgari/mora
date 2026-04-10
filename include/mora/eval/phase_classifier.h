#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include <vector>
#include <unordered_set>

namespace mora {

enum class Phase { Static, Dynamic };

struct RuleClassification {
    StringId rule_name;
    Phase phase;
    std::string reason;
};

class PhaseClassifier {
public:
    explicit PhaseClassifier(StringPool& pool);
    Phase classify(const Rule& rule) const;
    std::vector<RuleClassification> classify_module(const Module& mod) const;

private:
    bool is_instance_fact(StringId name) const;
    bool body_has_instance_facts(const Rule& rule) const;
    std::unordered_set<uint32_t> instance_facts_;
};

} // namespace mora
