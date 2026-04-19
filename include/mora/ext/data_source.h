#pragma once

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mora {
class FactDB;
}

namespace mora::ext {

// Runtime context handed to DataSource::load. Carries the StringPool,
// diagnostic sink, CLI-supplied paths, and anything else loaders need
// that isn't the FactDB itself.
//
// `data_dir` and `plugins_txt` are concessions to the current Skyrim
// ESP loader — they need a data-root path and an optional plugins.txt
// manifest. When a non-Skyrim DataSource lands (e.g. parquet source in
// Plan 4), these move behind a typed-opaque slot so each extension
// owns its own context shape. For now, keeping them here avoids a
// second plumbing layer.
struct LoadCtx {
    StringPool& pool;
    DiagBag&    diags;

    // --data-dir equivalent: root directory for file-system-backed sources.
    std::filesystem::path data_dir;

    // --plugins-txt equivalent: optional load-order manifest path.
    // Empty when not specified.
    std::filesystem::path plugins_txt;

    // The set of relation names (as interned StringId values) that sema
    // says the program references. Loaders MAY use this to skip work
    // for relations nobody asked for.
    std::unordered_set<uint32_t> needed_relations;

    // Option B output: if non-null, load() populates this map with
    // EditorID → FormID entries accumulated during extraction. The
    // caller (main.cpp) feeds the map into the evaluator after
    // load_required() returns. Null = caller doesn't need editor IDs.
    std::unordered_map<std::string, uint32_t>* editor_ids_out = nullptr;

    // Option B output: if non-null, load() inserts every plugin
    // filename it loads into this set. The caller uses it to build the
    // load-order manifest for the patch-file digest and for
    // `requires mod(...)` validation. Null = caller doesn't need the set.
    std::unordered_set<std::string>* loaded_plugins_out = nullptr;

    // KID (*_KID.ini) ingestion knobs, honored by mora_skyrim_compile's
    // KidDataSource. Defaults: enabled, scan `data_dir`. A dedicated
    // typed-opaque slot per extension is the long-term plan (see note
    // at the top of this struct); these fields stay flat for v1 to
    // avoid introducing a second plumbing layer ahead of need.
    bool                         kid_enabled = true;
    std::filesystem::path        kid_dir;  // empty = use data_dir
};

// A DataSource produces tuples into a FactDB. Loaders are registered
// with the ExtensionContext at extension registration time and invoked
// by the core loader during program execution.
class DataSource {
public:
    virtual ~DataSource() = default;

    // Stable identifier (e.g. "skyrim.esp"). Used in diagnostics and
    // for --prefer-source disambiguation.
    virtual std::string_view name() const = 0;

    // Which relations this source can fill. Returned as a span of
    // relation names (pool-agnostic). ExtensionContext::load_required
    // interns each name into ctx.pool at dispatch time and intersects
    // the resulting ids with LoadCtx::needed_relations. Returning names
    // rather than pool-specific ids lets a DataSource be registered
    // once at startup and dispatched against an arbitrary per-run pool.
    virtual std::span<const std::string> provides() const = 0;

    // Do the work: load facts into `out`. Errors report through
    // ctx.diags; returning doesn't imply success — check diags.
    virtual void load(LoadCtx& ctx, FactDB& out) = 0;
};

} // namespace mora::ext
