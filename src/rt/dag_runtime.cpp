#include "mora/rt/dag_runtime.h"
#include "mora/dag/bytecode.h"

namespace mora::rt {

bool DagRuntime::init_from(const MappedPatchFile& mpf) {
    auto sec = mpf.section(emit::SectionId::DagBytecode);
    if (sec.data) {
        graph_ = dag::deserialize_dag(sec.data, sec.size);
    }
    engine_ = std::make_unique<DagEngine>(graph_, registry_);
    return true;
}

} // namespace mora::rt
