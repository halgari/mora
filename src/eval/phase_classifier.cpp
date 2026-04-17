#include "mora/eval/phase_classifier.h"
#include "mora/model/relations.h"
#include <string>
#include <variant>

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

PhaseClassifier::PhaseClassifier(StringPool& pool) : pool_(&pool) {
    for (const char* name : kInstanceFacts) {
        StringId const id = pool.intern(name);
        instance_facts_.insert(id.index);
    }
}

PhaseClassifier::PhaseClassifier(StringPool& pool, DiagBag& diag)
    : pool_(&pool), diag_(&diag) {
    for (const char* name : kInstanceFacts) {
        StringId const id = pool.intern(name);
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
        Phase const phase = classify(rule);
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

namespace {

PhaseClass classify_rule_v2(const Rule& r, StringPool& pool, DiagBag* diag) {
    if (r.kind == RuleKind::Maintain) return PhaseClass::Maintain;
    if (r.kind == RuleKind::On)       return PhaseClass::On;

    // Unannotated: require every body relation to be Static-sourced.
    bool dynamic_seen = false;
    for (const auto& c : r.body) {
        if (!std::holds_alternative<FactPattern>(c.data)) continue;
        const auto& fp = std::get<FactPattern>(c.data);
        std::string const ns{pool.get(fp.qualifier)};
        std::string const nm{pool.get(fp.name)};
        if (ns.empty()) continue;
        const auto* rel = model::find_relation(ns, nm,
            model::kRelations, model::kRelationCount);
        if (!rel) continue;
        if (rel->source != model::RelationSourceKind::Static) {
            dynamic_seen = true;
            break;
        }
    }
    if (dynamic_seen && diag != nullptr) {
        diag->error(
            "E_PHASE_UNANNOTATED",
            "rule has dynamic relations in its body but is not annotated 'maintain' or 'on'",
            r.span,
            "");
    }
    return PhaseClass::Static;
}

} // anonymous

std::unordered_map<size_t, PhaseClass> PhaseClassifier::classify(const Module& m) {
    std::unordered_map<size_t, PhaseClass> out;
    for (size_t i = 0; i < m.rules.size(); ++i) {
        out[i] = classify_rule_v2(m.rules[i], *pool_, diag_);
    }
    return out;
}

} // namespace mora
