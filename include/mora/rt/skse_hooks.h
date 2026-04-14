#pragma once
#include "mora/rt/dag_runtime.h"
#include <string>
#include <unordered_set>

namespace mora::rt {

// Binds SKSE event sinks to feed deltas into the DAG engine. Only hooks
// whose name appears in `needed_hooks` are installed; everything else is
// left alone. Windows-only; Linux build compiles a no-op.
void register_skse_hooks(DagRuntime& dr,
                         const std::unordered_set<std::string>& needed_hooks);

// Accessor for the process-global DagRuntime owned by patch_walker.cpp.
// Implemented in patch_walker.cpp (Windows) / stubbed in the Linux build.
DagRuntime& get_global_dag_runtime();

} // namespace mora::rt
