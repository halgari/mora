#include "mora/dag/bytecode.h"
#include <cstring>

namespace mora::dag {

static constexpr uint32_t kDagMagic = 0x31474144u; // 'DAG1' little-endian

std::vector<uint8_t> serialize_dag(const DagGraph& g) {
    uint32_t header[2] = { kDagMagic, static_cast<uint32_t>(g.node_count()) };
    size_t total = sizeof(header) + g.node_count() * sizeof(DagNode);
    std::vector<uint8_t> out(total);
    std::memcpy(out.data(), header, sizeof(header));
    if (g.node_count())
        std::memcpy(out.data() + sizeof(header), g.nodes().data(),
                    g.node_count() * sizeof(DagNode));
    return out;
}

DagGraph deserialize_dag(const uint8_t* data, size_t size) {
    DagGraph g;
    if (size < 8) return g;
    uint32_t magic = 0, count = 0;
    std::memcpy(&magic, data, 4);
    std::memcpy(&count, data + 4, 4);
    if (magic != kDagMagic) return g;
    if (size < 8 + count * sizeof(DagNode)) return g;
    for (uint32_t i = 0; i < count; ++i) {
        DagNode n;
        std::memcpy(&n, data + 8 + i * sizeof(DagNode), sizeof(DagNode));
        g.add_node(n);
    }
    return g;
}

} // namespace mora::dag
