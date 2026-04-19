#include "mora_skyrim_runtime/register.h"
#include "mora_skyrim_runtime/runtime_snapshot_sink.h"

#include "mora/ext/extension.h"

#include <memory>

namespace mora_skyrim_runtime {

void register_skyrim_runtime(mora::ext::ExtensionContext& ctx) {
    // Register the runtime-snapshot sink so `--sink skyrim_runtime.snapshot=<out>`
    // on the CLI writes the flat binary MoraRuntime.dll reads at DataLoaded.
    ctx.register_sink(std::make_unique<RuntimeSnapshotSink>());
}

} // namespace mora_skyrim_runtime
