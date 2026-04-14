#include "mora/rt/needed_handlers.h"
#include "mora/dag/opcode.h"
#include "mora/dag/node.h"
#include "mora/model/relations.h"

namespace mora::rt {

static inline void maybe_add(std::unordered_set<uint16_t>& s, model::HandlerId id) {
    if (id != model::HandlerId::None) s.insert(static_cast<uint16_t>(id));
}

std::unordered_set<uint16_t> needed_handler_ids(const dag::DagGraph& g) {
    std::unordered_set<uint16_t> out;
    for (uint32_t i = 0; i < g.node_count(); ++i) {
        const auto& n = g.node(i);
        switch (n.opcode) {
            case dag::DagOpcode::MaintainSink:
            case dag::DagOpcode::OnSink:
                maybe_add(out, n.handler_id);
                break;
            case dag::DagOpcode::EventSource:
            case dag::DagOpcode::StateSource:
            case dag::DagOpcode::StaticProbe:
                if (n.relation_id < model::kRelationCount) {
                    const auto& r = model::kRelations[n.relation_id];
                    maybe_add(out, r.apply_handler);
                    maybe_add(out, r.retract_handler);
                    maybe_add(out, r.read_handler);
                }
                break;
            default: break;
        }
    }
    return out;
}

std::unordered_set<std::string> needed_hook_names(const dag::DagGraph& g) {
    std::unordered_set<std::string> out;
    for (uint32_t i = 0; i < g.node_count(); ++i) {
        const auto& n = g.node(i);
        if (n.opcode != dag::DagOpcode::EventSource
            && n.opcode != dag::DagOpcode::StateSource) continue;
        if (n.relation_id >= model::kRelationCount) continue;
        const auto& r = model::kRelations[n.relation_id];
        if (!r.hook.hook_name.empty()) {
            out.emplace(r.hook.hook_name);
        }
    }
    return out;
}

} // namespace mora::rt
