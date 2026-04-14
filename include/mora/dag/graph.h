#pragma once
#include "mora/dag/node.h"
#include <cstdint>
#include <vector>

namespace mora::dag {

class DagGraph {
public:
    uint32_t add_node(DagNode n);
    void     set_input(uint32_t node_id, uint8_t slot, uint32_t source_id);
    const DagNode& node(uint32_t id) const { return nodes_[id]; }
    DagNode&       node(uint32_t id)       { return nodes_[id]; }
    size_t         node_count() const      { return nodes_.size(); }
    const std::vector<DagNode>& nodes() const { return nodes_; }
    std::vector<uint32_t> topological_order() const;
private:
    std::vector<DagNode> nodes_;
};

} // namespace mora::dag
