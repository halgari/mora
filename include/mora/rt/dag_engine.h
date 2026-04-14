#pragma once
#include "mora/dag/graph.h"
#include "mora/rt/arrangement_view.h"
#include "mora/rt/delta.h"
#include "mora/rt/handler_registry.h"
#include <unordered_map>
#include <vector>

namespace mora::rt {

class DagEngine {
public:
    DagEngine(const dag::DagGraph& g, HandlerRegistry& reg);
    void inject_delta(uint32_t source_node_id, Delta d);
    void run_to_quiescence();

    // Register a static arrangement for a StaticProbe node.
    void register_arrangement(uint32_t probe_node_id,
                              const uint8_t* data, size_t size,
                              uint8_t key_col_in_delta);

private:
    std::vector<std::vector<uint32_t>> consumers_;
    std::unordered_map<uint64_t, EffectHandle> maintain_state_;
    std::unordered_map<uint32_t, ArrangementView> probes_;
    std::unordered_map<uint32_t, uint8_t> probe_key_cols_;

    const dag::DagGraph& graph_;
    HandlerRegistry& reg_;
    DeltaQueue queue_;

    void propagate(uint32_t node_id, const Delta& d);
    void fire_sink(const dag::DagNode& node, const Delta& d);
    uint64_t maintain_key(uint32_t node_id, const TupleU32& t) const;
};

} // namespace mora::rt
