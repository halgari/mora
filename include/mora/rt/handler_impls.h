#pragma once
#include "mora/rt/handler_registry.h"
#include <cstdint>
#include <unordered_set>

namespace mora::rt {

// Binds concrete CommonLibSSE-NG implementations to HandlerId entries
// actually referenced by the loaded DAG. `needed` is the set of raw
// HandlerId values returned by rt::needed_handler_ids(graph). Handlers
// not in the set are not installed, so their SKSE state is never touched.
// Only does real work on Windows; the Linux build compiles a stub.
void bind_all_handlers(HandlerRegistry& reg,
                       const std::unordered_set<uint16_t>& needed);

} // namespace mora::rt
