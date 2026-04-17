#include "mora/rt/dag_engine.h"

namespace mora::rt {

DagEngine::DagEngine(const dag::DagGraph& g, HandlerRegistry& reg)
    : graph_(g), reg_(reg) {
    consumers_.resize(g.node_count());
    for (uint32_t i = 0; i < g.node_count(); ++i) {
        const auto& n = g.node(i);
        for (uint8_t s = 0; s < n.input_count; ++s) {
            consumers_[n.inputs[s]].push_back(i);
        }
    }
}

void DagEngine::inject_delta(uint32_t source_node_id, Delta d) {
    queue_.push(source_node_id, std::move(d));
}

uint64_t DagEngine::maintain_key(uint32_t node_id, const TupleU32& t) {
    uint64_t h = uint64_t(node_id) * 0x9E3779B185EBCA87ULL;
    for (auto v : t) h = (h * 0x100000001B3ULL) + v;
    return h;
}

void DagEngine::fire_sink(const dag::DagNode& node, const Delta& d) {
    EffectArgs const args{.args = d.tuple};
    if (node.opcode == dag::DagOpcode::OnSink) {
        if (d.diff > 0) reg_.invoke_effect(node.handler_id, args);
    } else if (node.opcode == dag::DagOpcode::MaintainSink) {
        uint64_t const key = maintain_key(node.node_id, d.tuple);
        if (d.diff > 0) {
            auto h = reg_.invoke_effect(node.handler_id, args);
            maintain_state_[key] = h;
        } else {
            auto it = maintain_state_.find(key);
            if (it != maintain_state_.end()) {
                reg_.invoke_retract(node.handler_id, it->second);
                maintain_state_.erase(it);
            }
        }
    }
}

void DagEngine::register_arrangement(uint32_t node_id,
                                     const uint8_t* data, size_t size,
                                     uint8_t key_col_in_delta) {
    probes_.emplace(node_id, ArrangementView(data, size));
    probe_key_cols_[node_id] = key_col_in_delta;
}

void DagEngine::propagate(uint32_t node_id, const Delta& d) {
    const auto& node = graph_.node(node_id);
    if (node.opcode == dag::DagOpcode::OnSink
        || node.opcode == dag::DagOpcode::MaintainSink) {
        fire_sink(node, d);
        return;
    }
    if (node.opcode == dag::DagOpcode::StaticProbe) {
        auto it = probes_.find(node_id);
        if (it == probes_.end()) return;  // no arrangement: drop delta
        uint8_t const kc = probe_key_cols_[node_id];
        if (d.tuple.size() <= kc) return;
        uint32_t const key = d.tuple[kc];
        auto rng = it->second.equal_range_u32(key);
        if (rng.count == 0) return;  // filter: no match
        for (uint32_t const c : consumers_[node_id]) {
            queue_.push(c, d);
        }
        return;
    }
    // Pass-through for sources and not-yet-implemented operators.
    for (uint32_t const c : consumers_[node_id]) {
        queue_.push(c, d);
    }
}

void DagEngine::run_to_quiescence() {
    while (!queue_.empty()) {
        auto [nid, d] = queue_.pop();
        propagate(nid, d);
    }
}

} // namespace mora::rt
