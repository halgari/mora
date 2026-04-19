#include "mora_skyrim_compile/esp_data_source.h"

#include "mora_skyrim_compile/esp/esp_reader.h"
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/mmap_file.h"
#include "mora_skyrim_compile/esp/override_filter.h"
#include "mora_skyrim_compile/esp/plugin_index.h"
#include "mora_skyrim_compile/plugin_facts.h"
#include "mora/cli/log.h"
#include "mora/core/string_pool.h"
#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"
#include "mora/ext/runtime_index.h"

#include <algorithm>
#include <fmt/format.h>
#include <future>
#include <thread>
#include <unordered_map>

namespace mora_skyrim_compile {

SkyrimEspDataSource::SkyrimEspDataSource() {
    // Enumerate the Skyrim default schema once and record names for
    // every relation with a non-empty ESP source spec. Names are
    // pool-agnostic strings; the caller's pool is used at dispatch
    // time (see ExtensionContext::load_required) and again inside
    // load() when we rebuild the SchemaRegistry against ctx.pool.
    mora::StringPool tmp_pool;
    mora::SchemaRegistry tmp_schema(tmp_pool);
    tmp_schema.register_defaults();
    for (const auto* rs : tmp_schema.all_schemas()) {
        if (!rs->esp_sources.empty()) {
            provides_.emplace_back(tmp_pool.get(rs->name));
        }
    }
    std::sort(provides_.begin(), provides_.end());
    provides_.erase(std::unique(provides_.begin(), provides_.end()),
                    provides_.end());
}

SkyrimEspDataSource::~SkyrimEspDataSource() = default;

std::string_view SkyrimEspDataSource::name() const {
    return "skyrim.esp";
}

std::span<const std::string> SkyrimEspDataSource::provides() const {
    return provides_;
}

void SkyrimEspDataSource::load(mora::ext::LoadCtx& ctx, mora::FactDB& out) {
    // Body is a verbatim move of the pre-M3 load_esp_data() orchestration
    // from src/main.cpp (lines 385–512), with these renames applied:
    //   cr.pool          → ctx.pool
    //   cr.diags         → ctx.diags
    //   data_dir         → ctx.data_dir.string()
    //   plugins_txt      → ctx.plugins_txt.string()
    //   needed           → ctx.needed_relations
    //   db               → out
    //   schema           → local schema bound to ctx.pool (built below)
    //   editor_id_map    → local map; merged into ctx.editor_ids_out if non-null
    //   loaded_plugins   → merged into ctx.loaded_plugins_out if non-null
    //   evaluator.set_symbol_formid() loop → removed (Option B: caller feeds
    //       the returned editor_ids_out map into the evaluator after load_required)
    //   out.phase_start/phase_done   → removed (progress is the driver's concern)

    // Rebuild the SchemaRegistry against the caller's pool on every
    // invocation so relation-name indices match the pool used by
    // needed_relations / FactDB / evaluator. Plan 5 replaces this with
    // extension-owned schema registration through ExtensionContext.
    mora::SchemaRegistry schema(ctx.pool);
    schema.register_defaults();
    schema.configure_fact_db(out);

    mora::LoadOrder lo = !ctx.plugins_txt.empty()
        ? mora::LoadOrder::from_plugins_txt(ctx.plugins_txt.string(), ctx.data_dir.string())
        : mora::LoadOrder::from_directory(ctx.data_dir.string());
    if (ctx.loaded_plugins_out) {
        for (auto& p : lo.plugins) {
            ctx.loaded_plugins_out->insert(p.filename().string());
        }
    }
    if (!ctx.plugins_txt.empty()) {
        mora::log::info("  Load order:    {} ({} plugins)\n",
            ctx.plugins_txt.string(), lo.plugins.size());
    }

    // Surface any plugins.txt entries that weren't found on disk.
    for (auto& name : lo.missing) {
        ctx.diags.warning(
            "plugin-missing",
            fmt::format("plugins.txt entry \"{}\" has no matching file under {} "
                        "— skipped (case-insensitive search tried)",
                        name, ctx.data_dir.string()),
            mora::SourceSpan{}, "");
    }
    if (!lo.missing.empty()) {
        mora::log::warn("  {} plugin(s) in plugins.txt couldn't be resolved on disk; see warnings\n",
            lo.missing.size());
    }

    auto runtime_index = lo.runtime_index_map();

    // ── Phase 1: parallel parse ────────────────────────────────────
    // Cache (MmapFile, PluginInfo) for every plugin so Phase 3 can
    // emit facts without re-walking GRUPs.
    struct Parsed {
        mora::MmapFile file;
        mora::PluginInfo info;
        uint32_t load_idx = 0;  // scalar synthetic idx for the filter
    };
    auto parse_one = [](const std::filesystem::path& path) {
        mora::MmapFile f(path.string());
        auto info = mora::build_plugin_index(f, path.filename().string());
        return Parsed{std::move(f), std::move(info), 0};
    };
    std::vector<std::future<Parsed>> parse_futures;
    parse_futures.reserve(lo.plugins.size());
    for (auto& p : lo.plugins) {
        parse_futures.push_back(std::async(std::launch::async, parse_one, p));
    }
    std::vector<Parsed> parsed;
    parsed.reserve(lo.plugins.size());
    for (auto& fut : parse_futures) {
        parsed.push_back(fut.get());
    }
    // Assign a scalar load idx per plugin. We hand the override
    // filter a monotonic index (position in lo.plugins) rather than
    // the rtmap high-byte because two light plugins can share a high
    // byte (0xFE) and we still need to order them against each other.
    for (size_t i = 0; i < parsed.size(); i++) {
        parsed[i].load_idx = static_cast<uint32_t>(i);
    }

    // ── Phase 2: override filter ───────────────────────────────────
    std::vector<mora::PluginInfo> infos;
    std::vector<uint32_t> load_idxs;
    infos.reserve(parsed.size());
    load_idxs.reserve(parsed.size());
    for (auto& p : parsed) {
        infos.push_back(p.info);
        load_idxs.push_back(p.load_idx);
    }
    auto override_filter = mora::OverrideFilter::build(infos, runtime_index, load_idxs);

    // Plugin-level facts (exists/load_index/is_master/is_light/
    // master_of/version/extension) land in the shared FactDB before
    // the per-plugin fact extraction kicks off. They're shared
    // across rules regardless of which mods a rule touches.
    mora::populate_plugin_facts(out, ctx.pool, lo, infos, runtime_index);

    // ── Phase 3: parallel fact extraction ──────────────────────────
    auto hw = std::max(1U, std::thread::hardware_concurrency());
    size_t const batch_size = (parsed.size() + hw - 1) / hw;

    struct BatchResult {
        mora::FactDB local_db;
        std::unordered_map<std::string, uint32_t> editor_ids;
        BatchResult(mora::StringPool& p) : local_db(p) {}
    };

    std::vector<std::future<BatchResult>> extract_futures;
    for (size_t i = 0; i < parsed.size(); i += batch_size) {
        size_t const end = std::min(i + batch_size, parsed.size());
        extract_futures.push_back(std::async(std::launch::async,
            [&, i, end]() -> BatchResult {
                BatchResult result(ctx.pool);
                schema.configure_fact_db(result.local_db);
                for (size_t k = i; k < end; k++) {
                    mora::EspReader reader(ctx.pool, ctx.diags, schema);
                    reader.set_needed_relations(ctx.needed_relations);
                    reader.set_runtime_index_map(&runtime_index);
                    reader.set_override_filter(&override_filter, parsed[k].load_idx);
                    reader.extract_from(parsed[k].file, parsed[k].info, result.local_db);
                    for (auto& [edid, fid] : reader.editor_id_map()) {
                        result.editor_ids[edid] = fid;
                    }
                }
                return result;
            }));
    }

    std::unordered_map<std::string, uint32_t> editor_id_map;
    for (auto& fut : extract_futures) {
        auto result = fut.get();
        out.merge_from(result.local_db);
        for (auto& [edid, formid] : result.editor_ids) {
            editor_id_map[edid] = formid;
        }
    }

    // Option B: populate the caller-supplied editor_ids_out map so
    // main.cpp can feed it into the evaluator after load_required returns.
    if (ctx.editor_ids_out) {
        for (auto& [edid, formid] : editor_id_map) {
            (*ctx.editor_ids_out)[edid] = formid;
        }
    }

    // Option B: expose the plugin runtime-index map in the packed
    // descriptor format documented in mora/ext/runtime_index.h. KID /
    // SPID resolvers consume this to translate `0xNNN~Plugin.ext`
    // references; leaving it nullptr is fine for callers that don't
    // need FormID-ref resolution.
    if (ctx.plugin_runtime_index_out) {
        for (auto& [name, idx] : runtime_index.index) {
            uint32_t descriptor = idx;
            if (runtime_index.light.contains(name)) {
                descriptor |= mora::ext::kRuntimeIdxEsl;
            }
            (*ctx.plugin_runtime_index_out)[name] = descriptor;
        }
    }
}

} // namespace mora_skyrim_compile
