#pragma once
#include "mora/rt/dag_runtime.h"

namespace mora::rt {

// Binds SKSE event sinks to feed deltas into the DAG engine.
// Windows-only; Linux build compiles a no-op.
void register_skse_hooks(DagRuntime& dr);

// Accessor for the process-global DagRuntime owned by patch_walker.cpp.
// Implemented in patch_walker.cpp (Windows) / stubbed in the Linux build.
DagRuntime& get_global_dag_runtime();

} // namespace mora::rt
