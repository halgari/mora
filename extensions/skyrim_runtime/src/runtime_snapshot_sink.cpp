#include "mora_skyrim_runtime/runtime_snapshot_sink.h"
#include "mora_skyrim_runtime/runtime_snapshot.h"

#include "mora/diag/diagnostic.h"

#include <filesystem>

namespace mora_skyrim_runtime {

std::string_view RuntimeSnapshotSink::name() const {
    return "skyrim_runtime.snapshot";
}

void RuntimeSnapshotSink::emit(mora::ext::EmitCtx& ctx,
                                const mora::FactDB& db)
{
    if (ctx.config.empty()) {
        ctx.diags.error("runtime-snapshot-config",
            "skyrim_runtime.snapshot requires a config path: "
            "--sink skyrim_runtime.snapshot=<out-dir|out-file>",
            mora::SourceSpan{}, "");
        return;
    }

    std::filesystem::path path(ctx.config);
    std::error_code ec;
    // If config points at an existing directory, or looks like one
    // (trailing slash, or no extension), append mora_runtime.bin.
    bool is_dir_like = false;
    if (std::filesystem::is_directory(path, ec)) is_dir_like = true;
    else if (!path.has_extension() && path.extension() != ".bin") is_dir_like = true;

    if (is_dir_like) {
        std::filesystem::create_directories(path, ec);
        path /= "mora_runtime.bin";
    } else {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    (void)write_snapshot(path, db, ctx.pool, ctx.diags);
}

} // namespace mora_skyrim_runtime
