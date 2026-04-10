#include "mora/eval/phase_classifier.h"

namespace mora {

static constexpr const char* kInstanceFacts[] = {
    "current_level",
    "current_location",
    "current_cell",
    "equipped",
    "in_inventory",
    "quest_stage",
    "is_alive",
};

PhaseClassifier::PhaseClassifier(StringPool& pool) {
    for (const char* name : kInstanceFacts) {
        StringId id = pool.intern(name);
        instance_facts_.insert(id.index);
    }
}

bool PhaseClassifier::is_instance_fact(StringId name) const {
    return instance_facts_.count(name.index) > 0;
}

bool PhaseClassifier::body_has_instance_facts(const Rule& rule) const {
    for (const Clause& clause : rule.body) {
        if (const auto* fp = std::get_if<FactPattern>(&clause.data)) {
            if (is_instance_fact(fp->name)) {
                return true;
            }
        }
    }
    return false;
}

Phase PhaseClassifier::classify(const Rule& rule) const {
    if (body_has_instance_facts(rule)) {
        return Phase::Dynamic;
    }
    return Phase::Static;
}

std::vector<RuleClassification> PhaseClassifier::classify_module(const Module& mod) const {
    std::vector<RuleClassification> results;
    results.reserve(mod.rules.size());
    for (const Rule& rule : mod.rules) {
        Phase phase = classify(rule);
        std::string reason;
        if (phase == Phase::Dynamic) {
            reason = "body references instance-level fact";
        } else {
            reason = "no instance-level facts in body";
        }
        results.push_back(RuleClassification{rule.name, phase, std::move(reason)});
    }
    return results;
}

} // namespace mora
