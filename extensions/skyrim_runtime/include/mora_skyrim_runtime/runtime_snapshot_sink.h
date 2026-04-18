#pragma once

#include "mora/ext/sink.h"

namespace mora_skyrim_runtime {

// Writes a flat-binary runtime snapshot (`mora_runtime.bin`) consumed
// by MoraRuntime.dll at Skyrim's DataLoaded event. See
// `runtime_snapshot.h` for the on-disk format.
//
// Name: "skyrim_runtime.snapshot"
// Config: path to the output directory (or output file; if a directory,
//         writes `<dir>/mora_runtime.bin`). Relative paths resolve
//         against the caller's working directory.
class RuntimeSnapshotSink : public mora::ext::Sink {
public:
    std::string_view name() const override;
    void             emit(mora::ext::EmitCtx& ctx,
                          const mora::FactDB& db) override;
};

} // namespace mora_skyrim_runtime
