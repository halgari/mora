#include "mora/dag/compile.h"
#include "mora/model/relations.h"

namespace mora::dag {

namespace {

uint32_t relation_index(std::string_view ns, std::string_view name) {
    for (size_t i = 0; i < mora::model::kRelationCount; ++i) {
        const auto& r = mora::model::kRelations[i];
        if (r.namespace_ == ns && r.name == name) return static_cast<uint32_t>(i);
    }
    return static_cast<uint32_t>(-1);
}

} // anonymous

CompileResult compile_dynamic_rules(const Module& m, StringPool& pool, DagGraph& g) {
    CompileResult res{.success = true};

    for (const auto& rule : m.rules) {
        if (rule.kind == RuleKind::Static) continue;

        // 1. Find the source clause.
        uint32_t source_node = static_cast<uint32_t>(-1);
        for (const auto& c : rule.body) {
            if (!std::holds_alternative<FactPattern>(c.data)) continue;
            const auto& fp = std::get<FactPattern>(c.data);
            std::string const ns{pool.get(fp.qualifier)};
            std::string const nm{pool.get(fp.name)};
            auto rel_idx = relation_index(ns, nm);
            if (rel_idx == static_cast<uint32_t>(-1)) continue;
            const auto& rel = mora::model::kRelations[rel_idx];
            if (rule.kind == RuleKind::On
                && rel.source == mora::model::RelationSourceKind::Event) {
                source_node = g.add_node({.opcode = DagOpcode::EventSource,
                                          .relation_id = static_cast<uint16_t>(rel_idx)});
                break;
            }
            if (rule.kind == RuleKind::Maintain
                && rel.source != mora::model::RelationSourceKind::Static
                && rel.source != mora::model::RelationSourceKind::Event) {
                source_node = g.add_node({.opcode = DagOpcode::StateSource,
                                          .relation_id = static_cast<uint16_t>(rel_idx)});
                break;
            }
        }
        if (source_node == static_cast<uint32_t>(-1)) continue;

        // 2. Thread StaticProbes for form/* patterns (after the source).
        uint32_t prev = source_node;
        for (const auto& c : rule.body) {
            if (!std::holds_alternative<FactPattern>(c.data)) continue;
            const auto& fp = std::get<FactPattern>(c.data);
            std::string const ns{pool.get(fp.qualifier)};
            if (ns != "form") continue;
            std::string const nm{pool.get(fp.name)};
            auto rel_idx = relation_index(ns, nm);
            if (rel_idx == static_cast<uint32_t>(-1)) continue;
            uint32_t const probe = g.add_node({.opcode = DagOpcode::StaticProbe,
                                         .relation_id = static_cast<uint16_t>(rel_idx)});
            g.set_input(probe, 0, prev);
            prev = probe;
        }

        // 3. Terminal sinks (one per effect).
        DagOpcode const sink_op = (rule.kind == RuleKind::Maintain)
                            ? DagOpcode::MaintainSink
                            : DagOpcode::OnSink;
        for (const auto& e : rule.effects) {
            std::string const ns{pool.get(e.namespace_)};
            std::string const nm{pool.get(e.name)};
            auto rel_idx = relation_index(ns, nm);
            mora::model::HandlerId hid = mora::model::HandlerId::None;
            uint16_t rid = 0;
            if (rel_idx != static_cast<uint32_t>(-1)) {
                hid = mora::model::kRelations[rel_idx].apply_handler;
                rid = static_cast<uint16_t>(rel_idx);
            }
            uint32_t const sink = g.add_node({.opcode = sink_op,
                                        .relation_id = rid,
                                        .handler_id = hid});
            g.set_input(sink, 0, prev);
        }
    }

    return res;
}

} // namespace mora::dag
