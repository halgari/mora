#include <algorithm>

#include "mora/dag/graph.h"

namespace mora::dag {

uint32_t DagGraph::add_node(DagNode n) {
    uint32_t const id = static_cast<uint32_t>(nodes_.size());
    n.node_id = id;
    nodes_.push_back(n);
    return id;
}

void DagGraph::set_input(uint32_t node_id, uint8_t slot, uint32_t source_id) {
    auto& n = nodes_[node_id];
    n.inputs[slot] = source_id;
    n.input_count = std::max<int>(slot + 1, n.input_count);
}

std::vector<uint32_t> DagGraph::topological_order() const {
    // add_node assigns monotonic ids and inputs always reference earlier nodes,
    // so insertion order is already a valid topological order.
    std::vector<uint32_t> out(nodes_.size());
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint32_t>(i);
    return out;
}

} // namespace mora::dag
