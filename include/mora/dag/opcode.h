#pragma once
#include <cstdint>

namespace mora::dag {

enum class DagOpcode : uint16_t {
    Unknown       = 0,
    EventSource   = 1,
    StateSource   = 2,
    Filter        = 3,
    Project       = 4,
    HashJoin      = 5,
    StaticProbe   = 6,
    MaintainSink  = 7,
    OnSink        = 8,
};

constexpr uint8_t kMaxDagInputs = 2;
constexpr uint8_t kMaxDagParams = 4;

} // namespace mora::dag
