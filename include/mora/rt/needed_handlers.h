#pragma once
#include "mora/dag/graph.h"
#include "mora/model/handler_ids.h"
#include <cstdint>
#include <string>
#include <unordered_set>

namespace mora::rt {

// Walks the DAG and collects every HandlerId actually referenced:
//   - MaintainSink/OnSink contribute their apply/retract handler_id.
//   - EventSource/StateSource/StaticProbe contribute the underlying
//     relation's apply/retract/read handlers (from kRelations).
// Returns a set of raw uint16_t HandlerId values for O(1) lookup from
// bind_all_handlers().
std::unordered_set<uint16_t> needed_handler_ids(const dag::DagGraph& g);

// Walks the DAG and collects every SKSE hook name referenced by
// EventSource / StateSource nodes (from kRelations hook metadata).
std::unordered_set<std::string> needed_hook_names(const dag::DagGraph& g);

} // namespace mora::rt
