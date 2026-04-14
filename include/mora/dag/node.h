#pragma once
#include "mora/dag/opcode.h"
#include "mora/model/handler_ids.h"
#include <cstdint>

namespace mora::dag {

struct DagNode {
    uint32_t  node_id     = 0;
    DagOpcode opcode      = DagOpcode::Unknown;
    uint16_t  relation_id = 0;
    model::HandlerId handler_id = model::HandlerId::None;
    uint8_t   input_count = 0;
    uint8_t   pad[3]      = {0,0,0};
    uint32_t  inputs[kMaxDagInputs]{0, 0};
    uint32_t  params[kMaxDagParams]{0, 0, 0, 0};
};
static_assert(sizeof(DagNode) <= 64);

} // namespace mora::dag
