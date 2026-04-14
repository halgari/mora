#pragma once
#include "mora/dag/graph.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mora::dag {

std::vector<uint8_t> serialize_dag(const DagGraph& g);
DagGraph deserialize_dag(const uint8_t* data, size_t size);

} // namespace mora::dag
