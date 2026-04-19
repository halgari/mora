#include "mora/lsp/lsp.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"
#include "mora/cli/terminal.h"
#include "mora/cli/output.h"
#include "mora/cli/log.h"
#include "mora/core/string_pool.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/phase_classifier.h"
#include "mora/eval/evaluator.h"
#include "mora/model/relations.h"
#include <algorithm>
#include <unordered_map>
#include "mora/core/string_utils.h"
#include "mora/ext/extension.h"
#include "mora_skyrim_compile/register.h"
#include "mora_parquet/register.h"
#include "mora_skyrim_runtime/runtime.h"
#include "mora_skyrim_runtime/runtime_snapshot.h"
#include "mora_skyrim_runtime/game_api.h"
#include "mora_skyrim_runtime/register.h"
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/esp_reader.h"
#include "mora/data/schema_registry.h"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

#ifndef MORA_VERSION
#define MORA_VERSION "0.0.0-dev"
#endif

namespace fs = std::filesystem;

#include "mora/cli/doc_generator.h"

// Locate a Plugins.txt / plugins.txt alongside or near `data_dir`.
// Preference order: Data/../Plugins.txt (co-located, matches our
// self-hosted CI image layout) → conventional Proton AppData prefixes
// → Windows %LOCALAPPDATA%. Returns "" when no file is found; caller
// falls back to directory-walk ordering in that case.
static std::string detect_plugins_txt(const std::string& data_dir) {
    std::vector<fs::path> candidates;

    if (!data_dir.empty()) {
        fs::path const base(data_dir);
        // Directly adjacent to Data/ (matches /skyrim-base/Plugins.txt
        // on the self-hosted runner image and most portable layouts).
        if (base.has_parent_path()) {
            candidates.push_back(base.parent_path() / "Plugins.txt");
            candidates.push_back(base.parent_path() / "plugins.txt");
        }
        // Inside Data/ (rare, but some MO2 profiles place it there).
        candidates.push_back(base / "Plugins.txt");
        candidates.push_back(base / "plugins.txt");
    }

    if (const char* home = std::getenv("HOME")) {
        fs::path const h(home);
        // Steam + Proton prefix for Skyrim SE (app id 489830).
        candidates.push_back(h / ".local/share/Steam/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt");
        // GOG portable prefix layout used by scripts/deploy_runtime.sh.
        candidates.push_back(h / "Games/gog/the-elder-scrolls-v-skyrim-special-edition/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition GOG/Plugins.txt");
    }
    // Self-hosted CI runner warm-prefix layout (skyrim-runner image).
    // Compile runs before /tmp/prefix is staged, so we read from the
    // immutable source at /opt/warm-prefix.
    candidates.push_back("/opt/warm-prefix/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt");
    candidates.push_back("/tmp/prefix/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt");
    if (const char* localapp = std::getenv("LOCALAPPDATA")) {
        candidates.push_back(fs::path(localapp) / "Skyrim Special Edition" / "Plugins.txt");
    }

    for (auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(p, ec)) return p.string();
    }
    return "";
}

static std::string detect_skyrim_data_dir() {
    // Check common Steam library locations for Skyrim SE
    std::vector<fs::path> candidates;

    // Linux: standard Steam path
    if (const char* home = std::getenv("HOME")) {
        candidates.push_back(fs::path(home) / ".local/share/Steam/steamapps/common/Skyrim Special Edition/Data");
        candidates.push_back(fs::path(home) / ".steam/steam/steamapps/common/Skyrim Special Edition/Data");
    }

    // Linux: flatpak Steam
    if (const char* home = std::getenv("HOME")) {
        candidates.push_back(fs::path(home) / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data");
    }

    // Windows: common paths
    candidates.push_back("C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition/Data");
    candidates.push_back("C:/Program Files/Steam/steamapps/common/Skyrim Special Edition/Data");

    for (auto& path : candidates) {
        if (fs::exists(path / "Skyrim.esm")) {
            return path.string();
        }
    }
    return "";
}

static std::string read_file(const fs::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        mora::log::error("could not open file: {}\n", path.string());
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::vector<fs::path> find_mora_files(const fs::path& dir) {
    std::vector<fs::path> files;
    if (!fs::exists(dir)) return files;
    if (fs::is_regular_file(dir) && dir.extension() == ".mora") {
        files.push_back(dir);
        return files;
    }
    for (auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.path().extension() == ".mora") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// (address library lookup removed — no longer needed without LLVM codegen)

static std::string format_value(const mora::Value& v, const mora::StringPool& pool) {
    switch (v.kind()) {
        case mora::Value::Kind::FormID: return fmt::format("0x{:08X}", v.as_formid());
        case mora::Value::Kind::Int:    return fmt::format("{}", v.as_int());
        case mora::Value::Kind::Float:  return fmt::format("{}", v.as_float());
        case mora::Value::Kind::Bool:   return v.as_bool() ? "true" : "false";
        case mora::Value::Kind::String:  return fmt::format("\"{}\"", pool.get(v.as_string()));
        case mora::Value::Kind::Keyword: return fmt::format(":{}",    pool.get(v.as_keyword()));
        case mora::Value::Kind::Var:     return "?";
        default:                         return "<unknown>";
    }
}

// Collect all relation names referenced by rules (for lazy ESP loading).
//
// v3 removed the verb-keyword grammar (Plan 16), so effects no longer
// carry an explicit relation name like master's `add form/keyword(...)`.
// Instead, `skyrim/add(N, :Keyword, @ActorTypeNPC)` embeds the field
// (`Keyword`) as a keyword literal and the target (`@ActorTypeNPC`) as
// an EditorIdExpr. We infer the needed ESP relation from the field name
// so the ESP loader builds the EditorID→FormID table for that form type.
static std::unordered_set<uint32_t> collect_used_relations(
    const std::vector<mora::Module>& modules,
    mora::StringPool&                pool)
{
    // Field keyword → ESP relation name. When rules reference a field
    // like ":Keyword" in an effect head, we need the ESP loader to
    // populate that form-type's EditorID table.
    static const std::unordered_map<std::string_view, std::string_view>
        kFieldToRelation = {
            {"Keyword",      "keyword"},
            {"Spell",        "spell"},
            {"Perk",         "perk"},
            {"Faction",      "faction"},
            {"Shout",        "shout"},
            {"Race",         "race"},
            {"VoiceType",    "voice_type"},
            {"Outfit",       "outfit"},
            {"Skin",         "armor"},
            {"Class",        "class"},
            {"Enchantment",  "enchantment"},
        };

    std::unordered_set<uint32_t> used;
    auto add_relation = [&](std::string_view name) {
        used.insert(pool.intern(std::string(name)).index);
    };

    for (auto& mod : modules) {
        for (auto& rule : mod.rules) {
            // Body clauses
            for (auto& clause : rule.body) {
                if (auto* fact = std::get_if<mora::FactPattern>(&clause.data)) {
                    used.insert(fact->name.index);
                } else if (auto* or_c = std::get_if<mora::OrClause>(&clause.data)) {
                    for (auto& branch : or_c->branches) {
                        for (auto& fp : branch) used.insert(fp.name.index);
                    }
                }
            }
            // Qualified rule heads may embed a field keyword (arg index 1
            // by convention) whose name implies the ESP relation to load.
            // Example: `skyrim/add(N, :Keyword, @KW)` → needs "keyword"
            // loaded so @KW resolves at compile time.
            for (auto& ha : rule.head_args) {
                if (auto const* kl = std::get_if<mora::KeywordLiteral>(&ha.data)) {
                    auto name = pool.get(kl->value);
                    auto it = kFieldToRelation.find(name);
                    if (it != kFieldToRelation.end()) {
                        add_relation(it->second);
                    }
                }
            }
        }
    }
    return used;
}

// Kept as a no-op for callers that used to print it manually. CLI11
// now owns the help text — see `--help` / per-subcommand `-h`.
static void print_usage() {
    mora::log::info(
        "Usage: mora <command> [options] [path]\n"
        "Try `mora --help` for the full option list.\n");
}

// ── Types previously exported by the import layer ──────────────────────────

namespace mora {
using EditorIdMap = std::unordered_map<std::string, uint32_t>;
using PluginSet   = std::unordered_set<std::string>;
}

// ── Shared: parse + resolve + type-check pipeline ──────────────────────────

struct CheckResult {
    mora::StringPool pool;
    mora::DiagBag diags;
    std::vector<mora::Module> modules;
    std::vector<std::string> sources;  // kept alive for string_views
    std::vector<fs::path> files;
    size_t rule_count = 0;
};

static bool run_check_pipeline(
    CheckResult& result,
    const std::string& target_path,
    mora::Output& out,
    bool use_color)
{
    (void)use_color;
    result.files = find_mora_files(target_path);

    // Parse .mora files
    out.phase_start("Parsing");
    result.sources.reserve(result.files.size());
    for (auto& file : result.files) {
        result.sources.push_back(read_file(file));
        const std::string& source = result.sources.back();
        if (source.empty()) continue;
        mora::Lexer lexer(source, file.string(), result.pool, result.diags);
        mora::Parser parser(lexer, result.pool, result.diags);
        auto mod = parser.parse_module();
        mod.filename = file.string();
        mod.source = source;
        result.modules.push_back(std::move(mod));
    }

    if (result.modules.empty()) {
        mora::log::error("No .mora files found in {}\n", target_path);
        mora::log::info(
            "  hint: pass a directory containing .mora files, or a single .mora file path\n");
        return false;
    }

    out.phase_done(fmt::format("Parsing {} .mora files", result.files.size()));

    // Resolve
    out.phase_start("Resolving");
    mora::NameResolver resolver(result.pool, result.diags);
    for (auto& mod : result.modules) {
        resolver.resolve(mod);
        result.rule_count += mod.rules.size();
    }
    out.phase_done(fmt::format("Resolving {} rules", result.rule_count));

    // Render diagnostics if any
    if (!result.diags.all().empty()) {
        mora::DiagRenderer const renderer(use_color);
        mora::log::info("\n{}", renderer.render_all(result.diags));
        if (result.diags.error_count() > mora::DiagBag::kMaxErrors) {
            mora::log::info("\n  ... and {} more errors (showing first {})\n",
                result.diags.error_count() - mora::DiagBag::kMaxErrors,
                mora::DiagBag::kMaxErrors);
        }
        if (result.diags.warning_count() > mora::DiagBag::kMaxWarnings) {
            mora::log::info("\n  ... and {} more warnings (showing first {})\n",
                result.diags.warning_count() - mora::DiagBag::kMaxWarnings,
                mora::DiagBag::kMaxWarnings);
        }
    }

    return true;
}

// ── Commands ───────────────────────────────────────────────────────────────

static int cmd_check(const std::string& target_path, mora::Output& out, bool use_color) {
    auto start = std::chrono::steady_clock::now();
    out.print_header(MORA_VERSION);

    CheckResult cr;
    if (!run_check_pipeline(cr, target_path, out, use_color)) return 1;

    auto total_end = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - start).count();

    if (cr.diags.has_errors()) {
        out.failure(fmt::format("Failed with {} error(s) in {}ms", cr.diags.error_count(), total_ms));
        return 1;
    } else {
        auto msg = fmt::format("Checked {} rules in {}ms", cr.rule_count, total_ms);
        if (cr.diags.warning_count() > 0) {
            msg += fmt::format(" ({} warnings)", cr.diags.warning_count());
        }
        out.success(msg);
        return 0;
    }
}

// (ESP loading is now handled by mora_skyrim_compile::SkyrimEspDataSource
//  via ExtensionContext::load_required — see cmd_compile below.)

static int cmd_compile(const std::string& target_path, const std::string& output_dir,
                        const std::string& data_dir, const std::string& plugins_txt,
                        mora::Output& out, bool use_color,
                        const std::unordered_map<std::string, std::string>& sink_configs) {
    auto start = std::chrono::steady_clock::now();
    out.print_header(MORA_VERSION);

    CheckResult cr;
    if (!run_check_pipeline(cr, target_path, out, use_color)) return 1;

    if (cr.diags.has_errors()) {
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        out.failure(fmt::format("Failed with {} error(s) in {}ms \xe2\x80\x94 aborting compilation",
            cr.diags.error_count(), total_ms));
        return 1;
    }

    // Phase classification
    out.phase_start("Classifying");
    mora::PhaseClassifier const classifier(cr.pool);
    size_t static_count = 0;
    size_t dynamic_count = 0;

    for (auto& mod : cr.modules) {
        auto classifications = classifier.classify_module(mod);
        for (size_t i = 0; i < classifications.size(); i++) {
            if (classifications[i].phase == mora::Phase::Static) static_count++;
            else dynamic_count++;
        }
    }
    out.phase_done(fmt::format("{} static, {} dynamic", static_count, dynamic_count));

    // Setup evaluation
    out.phase_start("Evaluating");
    mora::FactDB db(cr.pool);
    mora::Evaluator evaluator(cr.pool, cr.diags, db);
    mora::EditorIdMap editor_id_map;
    mora::PluginSet loaded_plugins;

    // Registration is cheap (one unique_ptr per extension); dispatch is
    // gated below on --data-dir (load_required) and --sink (sinks loop).
    // Hoisted out of the data_dir branch so ext_ctx stays in scope for
    // the post-evaluation sink-dispatch loop.
    mora::ext::ExtensionContext ext_ctx;
    mora_skyrim_compile::register_skyrim(ext_ctx);
    mora_parquet::register_parquet(ext_ctx);
    mora_skyrim_runtime::register_skyrim_runtime(ext_ctx);

    if (!data_dir.empty()) {
        mora::ext::LoadCtx load_ctx{
            cr.pool,
            cr.diags,
            /*data_dir*/          fs::path(data_dir),
            /*plugins_txt*/       fs::path(plugins_txt),
            /*needed_relations*/  collect_used_relations(cr.modules, cr.pool),
            /*editor_ids_out*/    &editor_id_map,
            /*loaded_plugins_out*/&loaded_plugins,
        };

        ext_ctx.load_required(load_ctx, db);

        // Feed editor IDs accumulated by SkyrimEspDataSource into the
        // evaluator so symbol literals in .mora files resolve to FormIDs.
        for (auto& [edid, formid] : editor_id_map) {
            evaluator.set_symbol_formid(cr.pool.intern(edid), formid);
        }
    }

    // Enforce `requires mod("X")` — a module whose declared
    // dependencies aren't in the resolved load order gets a warning
    // and its rules are skipped (emitting facts rooted in forms from
    // missing plugins would produce patches that never apply).
    {
        // If no data-dir/plugins.txt resolved and modules still have
        // requires decls, tell the user once up front that every
        // check will fail for a common reason (they forgot --data-dir)
        // so the per-module warnings aren't mysterious.
        bool any_requires = false;
        for (auto& m : cr.modules) if (!m.requires_decls.empty()) { any_requires = true; break; }
        if (any_requires && loaded_plugins.empty()) {
            mora::log::warn(
                "  no --data-dir; can't validate `requires mod(...)` — every requires will fail\n");
        }

        std::unordered_set<std::string> loaded_lower;
        for (auto& name : loaded_plugins) {
            loaded_lower.insert(mora::to_lower(name));
        }
        size_t skipped = 0;
        cr.modules.erase(std::remove_if(cr.modules.begin(), cr.modules.end(),
            [&](const mora::Module& m) {
                for (auto& req : m.requires_decls) {
                    std::string const want = mora::to_lower(std::string(cr.pool.get(req.mod_name)));
                    if (loaded_lower.contains(want)) continue;
                    cr.diags.warning(
                        "requires-unmet",
                        fmt::format("required mod \"{}\" is not in the load order; "
                                    "module rules skipped", std::string(cr.pool.get(req.mod_name))),
                        req.span, "");
                    skipped++;
                    return true;
                }
                return false;
            }), cr.modules.end());
        if (skipped > 0) {
            mora::log::info("  Skipped {} module(s) with unmet `requires mod(...)` — see warnings\n", skipped);
        }
    }

    if (!cr.modules.empty()) {
        out.phase_start("Evaluating (.mora rules)");
        auto eval_progress = [&](size_t current, size_t total,
                                 [[maybe_unused]] std::string_view name) {
            out.progress_update(fmt::format("Evaluating rule {} / {} ...",
                                             current, total));
        };
        for (auto& mod : cr.modules) {
            evaluator.evaluate_module(mod, db, eval_progress);
        }
        out.progress_clear();
        out.phase_done("done");
    }

    // Dispatch configured sinks (e.g. --sink parquet.snapshot=./out).
    // Snapshot errors per-sink so a failure reports against the sink
    // that emitted it rather than against "some sink". First failing
    // sink short-circuits later sinks.
    for (const auto& sink : ext_ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        const auto errors_before_sink = cr.diags.error_count();
        mora::ext::EmitCtx emit_ctx{cr.pool, cr.diags, it->second, &ext_ctx};
        sink->emit(emit_ctx, db);
        if (cr.diags.error_count() > errors_before_sink) {
            mora::log::error("  sink '{}' emitted {} error(s); aborting\n",
                sink->name(),
                cr.diags.error_count() - errors_before_sink);
            mora::DiagRenderer const renderer(use_color);
            mora::log::info("\n{}", renderer.render_all(cr.diags));
            return 1;
        }
    }

    // Summary
    size_t effect_count = 0;
    for (const char* rel : {"skyrim/set", "skyrim/add",
                             "skyrim/remove", "skyrim/multiply"}) {
        effect_count += db.fact_count(cr.pool.intern(rel));
    }

    std::vector<mora::TableRow> summary;
    summary.push_back({"Frozen:", fmt::format("{} rules", static_count),
        fmt::format("\xe2\x86\x92 {} effect fact(s)", effect_count)});
    if (dynamic_count > 0) {
        summary.push_back({"Dynamic:", fmt::format("{} rules", dynamic_count), "(deferred to runtime)"});
    }
    if (cr.diags.error_count() > 0) {
        summary.push_back({"Errors:",
            mora::TermStyle::red(fmt::format("{}", cr.diags.error_count()), use_color), ""});
    }
    if (cr.diags.warning_count() > 0) {
        summary.push_back({"Warnings:",
            mora::TermStyle::yellow(fmt::format("{}", cr.diags.warning_count()), use_color), ""});
    }
    out.table(summary);

    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    auto msg = fmt::format("Compiled {} rules in {}ms", cr.rule_count, total_ms);
    if (cr.diags.warning_count() > 0) {
        msg += fmt::format(" ({} warnings)", cr.diags.warning_count());
    }
    out.success(msg);
    return 0;
}

static int cmd_apply(const std::string& input_dir) {
    namespace fs = std::filesystem;
    mora::StringPool pool;
    mora::DiagBag diags;
    mora_skyrim_runtime::MockGameAPI api;
    size_t count = 0;

    fs::path const dir(input_dir);
    fs::path const snap_path = dir / "mora_runtime.bin";

    if (fs::exists(snap_path)) {
        // Flat-binary snapshot — what the SKSE runtime DLL reads.
        auto snap = mora_skyrim_runtime::read_snapshot(snap_path, diags);
        if (snap) {
            count = mora_skyrim_runtime::apply_snapshot(*snap, api, pool);
        }
    } else {
        // Parquet-directory fallback — what `mora apply` originally read.
        count = mora_skyrim_runtime::runtime_apply(dir, api, pool, diags);
    }

    if (diags.has_errors()) {
        mora::DiagRenderer renderer(/*use_color*/true);
        mora::log::info("\n{}", renderer.render_all(diags));
        return 1;
    }
    mora::log::info("  Applied {} effect fact(s) via MockGameAPI\n", count);
    mora::log::info("  (Mock — no game state was modified)\n");
    return 0;
}

static int cmd_inspect(const std::string& target_path, bool show_conflicts,
                       bool use_color) {
    auto files = find_mora_files(target_path);
    if (files.empty()) {
        mora::log::error("No .mora files found in {}\n", target_path);
        mora::log::info("  hint: mora inspect reads .mora source files and displays the patch set\n");
        return 1;
    }

    mora::StringPool pool;
    mora::DiagBag diags;
    std::vector<mora::Module> modules;
    std::vector<std::string> sources;

    // Quick parse + resolve + type-check + evaluate
    sources.reserve(files.size());
    for (auto& file : files) {
        sources.push_back(read_file(file));
        const std::string& source = sources.back();
        if (source.empty()) continue;
        mora::Lexer lexer(source, file.string(), pool, diags);
        mora::Parser parser(lexer, pool, diags);
        auto mod = parser.parse_module();
        mod.filename = file.string();
        mod.source = source;
        modules.push_back(std::move(mod));
    }

    mora::NameResolver resolver(pool, diags);
    for (auto& mod : modules) resolver.resolve(mod);

    if (diags.has_errors()) {
        mora::DiagRenderer const renderer(use_color);
        mora::log::info("{}", renderer.render_all(diags));
        return 1;
    }

    mora::FactDB db(pool);
    mora::Evaluator evaluator(pool, diags, db);
    for (auto& mod : modules) {
        evaluator.evaluate_module(mod, db);
    }

    (void)show_conflicts;  // --show-conflicts is no longer meaningful

    struct EffectRel { const char* rel_name; const char* op_str; };
    constexpr EffectRel kEffectRels[] = {
        {"skyrim/set",      "="},
        {"skyrim/add",      "+="},
        {"skyrim/remove",   "-="},
        {"skyrim/multiply", "*="},
    };

    size_t total = 0;
    for (const auto& er : kEffectRels) {
        total += db.fact_count(pool.intern(er.rel_name));
    }

    mora::log::info("  mora inspect \xe2\x80\x94 {} effect fact(s) (from {} files)\n\n",
                     total, files.size());

    if (total == 0) {
        mora::log::info("  (no effect facts)\n");
        return 0;
    }

    for (const auto& er : kEffectRels) {
        auto rel_id = pool.intern(er.rel_name);
        const auto& tuples = db.get_relation(rel_id);
        if (tuples.empty()) continue;
        for (const auto& t : tuples) {
            if (t.size() < 3) continue;
            auto const formid = t[0].kind() == mora::Value::Kind::FormID
                                  ? t[0].as_formid() : 0u;
            auto const field_kw = t[1].kind() == mora::Value::Kind::Keyword
                                    ? pool.get(t[1].as_keyword())
                                    : std::string_view{"?"};
            mora::log::info("  0x{:08X}: {} {} {}\n",
                             formid, field_kw, er.op_str,
                             format_value(t[2], pool));
        }
    }

    return 0;
}

static int cmd_info(const std::string& target_path, const std::string& data_dir) {
    mora::log::info("  mora v0.1.0\n\n");

    fs::path base(target_path);
    if (fs::is_regular_file(base)) {
        base = base.parent_path();
    }

    auto files = find_mora_files(base);
    size_t rule_count = 0;

    mora::StringPool pool;
    mora::DiagBag diags;

    if (!files.empty()) {
        for (auto& file : files) {
            auto source = read_file(file);
            if (source.empty()) continue;
            mora::Lexer lexer(source, file.string(), pool, diags);
            mora::Parser parser(lexer, pool, diags);
            auto mod = parser.parse_module();
            rule_count += mod.rules.size();
        }
    }

    mora::log::info("  Mora rules:    {} across {} files\n", rule_count, files.size());

    if (!data_dir.empty()) {
        mora::LoadOrder const lo = mora::LoadOrder::from_directory(data_dir);
        mora::log::info("  Data dir:      {}\n", data_dir);
        mora::log::info("  Plugins found: {}\n", lo.plugins.size());

        if (!lo.plugins.empty()) {
            mora::FactDB db(pool);
            mora::SchemaRegistry schema(pool);
            schema.register_defaults();
            schema.configure_fact_db(db);
            mora::EspReader esp_reader(pool, diags, schema);
            esp_reader.read_load_order(lo.plugins, db);
            mora::log::info("  Facts loaded:  {}\n", db.fact_count());
        }
    }

    return 0;
}


// ── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) try {
    CLI::App app{"Mora — Datalog DSL compiler for Skyrim (SKSE)", "mora"};
    app.set_version_flag("--version,-V", std::string(MORA_VERSION));
    app.require_subcommand(1);
    app.set_help_all_flag("--help-all", "Show help for all subcommands");

    // Global option — color policy applies to every subcommand's output.
    bool force_no_color = false;
    app.add_flag("--no-color", force_no_color, "Disable colored output");

    // `check`
    std::string check_target = ".";
    auto* c_check = app.add_subcommand("check", "Type check and lint .mora files");
    c_check->fallthrough();
    c_check->add_option("path", check_target,
                        "File or directory containing .mora sources")
          ->default_val(".");

    // `compile`
    std::string comp_target = ".";
    // Empty default — filled in after parsing so we can tell "user passed
    // --output X" apart from "user relied on the default", which gates the
    // <Data>/SKSE/Plugins auto-install path below.
    std::string comp_output;
    std::string comp_data_dir;
    std::string comp_plugins_txt;
    std::vector<std::string> comp_sinks;
    auto* c_compile = app.add_subcommand("compile",
                                         "Compile .mora files and write effect facts to configured sinks");
    c_compile->fallthrough();
    c_compile->add_option("path", comp_target,
                          "File or directory containing .mora sources")
             ->default_val(".");
    c_compile->add_option("-o,--output", comp_output,
                          "Output directory (default: <Data>/SKSE/Plugins when detected, else MoraCache/)");
    c_compile->add_option("--data-dir", comp_data_dir,
                          "Skyrim Data/ directory for ESP loading (auto-detected if omitted)");
    c_compile->add_option("--plugins-txt", comp_plugins_txt,
                          "Path to Plugins.txt (authoritative load-order; auto-detected if omitted)");
    c_compile->add_option("--sink", comp_sinks,
        "Sink to invoke after evaluation. Repeatable. Format: "
        "<sink-name>=<config-string>, e.g. --sink parquet.snapshot=./out")
        ->expected(0, -1);

    // `inspect`
    std::string insp_target = ".";
    bool show_conflicts = false;
    auto* c_inspect = app.add_subcommand("inspect",
                                         "Display the patch set produced by .mora sources");
    c_inspect->fallthrough();
    c_inspect->add_option("path", insp_target,
                          "File or directory containing .mora sources")
             ->default_val(".");
    c_inspect->add_flag("--conflicts", show_conflicts,
                        "Show only conflict info");

    // `apply`
    std::string apply_parquet_dir;
    auto* c_apply = app.add_subcommand("apply",
                                       "Apply a parquet snapshot via MockGameAPI");
    c_apply->fallthrough();
    c_apply->add_option("parquet-dir", apply_parquet_dir,
                        "Directory containing skyrim/{set,add,remove,multiply}.parquet")
           ->required();

    // `info`
    std::string info_target = ".";
    std::string info_data_dir;
    auto* c_info = app.add_subcommand("info", "Show project status overview");
    c_info->fallthrough();
    c_info->add_option("path", info_target, "Project directory")
          ->default_val(".");
    c_info->add_option("--data-dir", info_data_dir,
                       "Skyrim Data/ directory (auto-detected if omitted)");

    // `docs`, `lsp` — no options exposed. `lsp` forwards unrecognized
    // args through to the language-server runner.
    auto* c_docs = app.add_subcommand("docs",
                                      "Print generated documentation to stdout");
    auto* c_lsp  = app.add_subcommand("lsp",
                                      "Run the language server over stdio (used by editors)");
    c_lsp->allow_extras();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    bool const use_color = !force_no_color && mora::color_enabled();
    bool const is_tty = mora::stdout_is_tty();
    mora::Output out(use_color, is_tty);

    if (*c_check) {
        return cmd_check(check_target, out, use_color);
    }
    if (*c_compile) {
        // Query CLI11 for whether each path-flag was explicitly passed —
        // separates "user opted in" from "defaulted" so we can tag each
        // entry in the summary and decide whether to auto-derive --output.
        bool const explicit_output      = c_compile->get_option("--output")->count()     > 0;
        bool const explicit_data_dir    = c_compile->get_option("--data-dir")->count()   > 0;
        bool const explicit_plugins_txt = c_compile->get_option("--plugins-txt")->count() > 0;

        // Auto-detect data-dir + plugins.txt when not explicitly provided.
        if (!explicit_data_dir) comp_data_dir = detect_skyrim_data_dir();
        if (!explicit_plugins_txt && !comp_data_dir.empty()) {
            comp_plugins_txt = detect_plugins_txt(comp_data_dir);
        }

        // Zero-arg DX: auto-derive output dir from data_dir
        // so the runtime picks it up on next game launch without a copy step.
        // Only fires when data_dir points at a real install (Skyrim.esm present)
        // — a bogus explicit --data-dir would otherwise cause a permission-
        // denied crash when we try to create the SKSE/Plugins subdir. Falls
        // back to cwd-relative MoraCache/ otherwise.
        if (!explicit_output) {
            std::error_code ec;
            if (!comp_data_dir.empty() &&
                fs::exists(fs::path(comp_data_dir) / "Skyrim.esm", ec)) {
                comp_output = (fs::path(comp_data_dir) / "SKSE" / "Plugins").string();
            } else {
                comp_output = "MoraCache";
                mora::log::warn("no Skyrim Data directory detected; writing to {}. "
                                "Pass --output or --data-dir to override.\n", comp_output);
            }
        }

        // Resolution summary — shows where bytes are landing without --verbose.
        auto tag = [](bool explicit_) { return explicit_ ? " (provided)" : " (auto)"; };
        mora::log::info("  data-dir:    {}{}\n",
            comp_data_dir.empty() ? "<none>" : comp_data_dir, tag(explicit_data_dir));
        mora::log::info("  plugins.txt: {}{}\n",
            comp_plugins_txt.empty() ? "<none>" : comp_plugins_txt, tag(explicit_plugins_txt));
        mora::log::info("  output:      {}{}\n", comp_output, tag(explicit_output));

        // Parse --sink entries into a map keyed by sink name. Duplicate
        // names (e.g. `--sink parquet.snapshot=a --sink parquet.snapshot=b`)
        // are a configuration mistake — warn and keep the last value.
        std::unordered_map<std::string, std::string> sink_configs;
        for (const auto& entry : comp_sinks) {
            auto eq = entry.find('=');
            if (eq == std::string::npos) {
                mora::log::error(
                    "--sink '{}' has no '='; expected <sink-name>=<config>\n",
                    entry);
                return 2;
            }
            auto name   = entry.substr(0, eq);
            auto config = entry.substr(eq + 1);
            if (auto prior = sink_configs.find(name); prior != sink_configs.end()) {
                mora::log::warn(
                    "--sink '{}' specified more than once; keeping last value '{}' (previous: '{}')\n",
                    name, config, prior->second);
            }
            sink_configs[name] = config;
        }

        return cmd_compile(comp_target, comp_output, comp_data_dir, comp_plugins_txt,
                           out, use_color, sink_configs);
    }
    if (*c_inspect) {
        return cmd_inspect(insp_target, show_conflicts, use_color);
    }
    if (*c_apply) {
        return cmd_apply(apply_parquet_dir);
    }
    if (*c_info) {
        if (info_data_dir.empty()) info_data_dir = detect_skyrim_data_dir();
        return cmd_info(info_target, info_data_dir);
    }
    if (*c_docs) {
        mora::generate_docs(std::cout);
        return 0;
    }
    if (*c_lsp) {
        auto remaining = c_lsp->remaining();
        std::vector<char*> lsp_argv;
        lsp_argv.reserve(remaining.size());
        for (auto& s : remaining) lsp_argv.push_back(s.data());
        return mora::lsp::run(static_cast<int>(lsp_argv.size()), lsp_argv.data());
    }

    // Unreachable — require_subcommand(1) guarantees one is parsed.
    print_usage();
    return 1;
} catch (const std::exception& e) {
    fmt::print(stderr, "mora: fatal: {}\n", e.what());
    return 1;
} catch (...) {
    fmt::print(stderr, "mora: fatal: unknown exception\n");
    return 1;
}
