#pragma once
#include "mora/rt/mapped_patch_file.h"
#include "mora/rt/dag_engine.h"
#include "mora/dag/graph.h"
#include "mora/rt/handler_registry.h"
#include <memory>

namespace mora::rt {

class DagRuntime {
public:
    bool init_from(const MappedPatchFile& mpf);
    const dag::DagGraph& dag() const { return graph_; }
    DagEngine* engine() { return engine_.get(); }
    HandlerRegistry& registry() { return registry_; }

private:
    dag::DagGraph graph_;
    HandlerRegistry registry_;
    std::unique_ptr<DagEngine> engine_;
};

} // namespace mora::rt
