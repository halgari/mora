#include "mora/lsp/lsp.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"
#include "mora/cli/terminal.h"
#include "mora/cli/output.h"
#include "mora/cli/log.h"
#include "mora/core/string_pool.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_buffer.h"
#include "mora/eval/phase_classifier.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/patch_set.h"
#include "mora/emit/patch_table.h"
#include "mora/emit/arrangement_emit.h"
#include "mora/model/relations.h"
#include "mora/model/field_names.h"
#include "mora/eval/effect_facts.h"
#include "mora/core/digest.h"
#include <algorithm>
#include <unordered_map>
#include "mora/core/string_utils.h"
#include "mora/ext/extension.h"
#include "mora_skyrim_compile/register.h"
#include "mora_parquet/register.h"
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

static std::string format_bytes(std::uintmax_t bytes) {
    if (bytes < 1024) return fmt::format("{} bytes", bytes);
    if (bytes < std::uintmax_t{1024} * 1024) return fmt::format("{:.1f} KB", double(bytes) / 1024.0);
    return fmt::format("{:.1f} MB", double(bytes) / (1024.0 * 1024.0));
}

// (address library lookup removed — no longer needed without LLVM codegen)

static std::string field_name(mora::FieldId id) {
    return mora::field_id_name(id);
}

static std::string op_prefix(mora::FieldOp op) {
    switch (op) {
        case mora::FieldOp::Set:      return "set";
        case mora::FieldOp::Add:      return "+";
        case mora::FieldOp::Remove:   return "-";
        case mora::FieldOp::Multiply: return "*";
        default:                      return "?";
    }
}

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

// Collect all relation names referenced by rules (for lazy ESP loading)
static std::unordered_set<uint32_t> collect_used_relations(
    const std::vector<mora::Module>& modules) {
    std::unordered_set<uint32_t> used;
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
            // Effects (actions map to relations too)
            for (auto& eff : rule.effects) {
                used.insert(eff.name.index);
            }
            for (auto& ce : rule.conditional_effects) {
                used.insert(ce.effect.name.index);
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

    // Type check
    out.phase_start("Type checking");
    mora::TypeChecker checker(result.pool, result.diags, resolver);
    for (auto& mod : result.modules) {
        checker.check(mod);
    }
    out.phase_done(fmt::format("Type checking {} rules", result.rule_count));

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

// Evaluate .mora rules via Datalog and merge results into PatchBuffer.
// Also produces a StringTable section (for String-valued patches) that the
// caller must pass to serialize_patch_table.
static void evaluate_mora_rules(
    CheckResult& cr, mora::Evaluator& evaluator,
    mora::PatchBuffer& patch_buf, std::vector<uint8_t>& string_table_out,
    mora::ResolvedPatchSet& out_resolved,
    mora::Output& out)
{
    string_table_out.clear();
    if (cr.modules.empty()) return;

    out.phase_start("Evaluating (.mora rules)");
    auto eval_progress = [&](size_t current, size_t total, [[maybe_unused]] std::string_view name) {
        out.progress_update(fmt::format("Evaluating rule {} / {} ...", current, total));
    };

    mora::PatchSet all_patches;
    for (auto& mod : cr.modules) {
        auto mod_patches = evaluator.evaluate_static(mod, eval_progress);
        auto resolved = mod_patches.resolve();
        for (auto& rp : resolved.all_patches_sorted()) {
            for (auto& fp : rp.fields) {
                all_patches.add_patch(rp.target_formid, fp.field, fp.op, fp.value, fp.source_mod, fp.priority);
            }
        }
    }
    out.progress_clear();
    out.phase_done("done");

    out_resolved = all_patches.resolve();
    std::vector<mora::PatchEntry> entries;
    string_table_out = mora::build_patch_entries_and_string_table(
        out_resolved, cr.pool, entries);
    for (const auto& e : entries) {
        patch_buf.emit(e.formid, e.field_id, e.op, e.value_type, e.value);
    }
    patch_buf.sort_and_dedup();
}

// Build arrangements section from FactDB for Static Set-valued relations.
// Plan 2: hard-coded mapping for form/keyword and form/faction.
static std::vector<uint8_t> build_static_arrangements_section(
    const mora::FactDB& facts, mora::StringPool& pool) {

    struct Mapping {
        std::string_view ns;
        std::string_view name;
        std::string_view factdb_key;
    };
    constexpr Mapping kMappings[] = {
        {"form", "keyword", "has_keyword"},
        {"form", "faction", "has_faction"},
    };

    std::vector<std::vector<uint8_t>> arrangements;
    for (size_t i = 0; i < mora::model::kRelationCount; ++i) {
        const auto& r = mora::model::kRelations[i];
        if (r.source != mora::model::RelationSourceKind::Static) continue;
        if (r.type.ctor != mora::model::TypeCtor::List) continue;

        std::string_view fdb_key;
        for (const auto& m : kMappings) {
            if (r.namespace_ == m.ns && r.name == m.name) {
                fdb_key = m.factdb_key;
                break;
            }
        }
        if (fdb_key.empty()) continue;

        auto rel_id = pool.intern(std::string{fdb_key});
        const auto& tuples = facts.get_relation(rel_id);
        if (tuples.empty()) continue;

        std::vector<std::array<uint32_t, 2>> rows;
        rows.reserve(tuples.size());
        for (const auto& t : tuples) {
            if (t.size() < 2) continue;
            uint32_t c0 = 0;
            uint32_t c1 = 0;
            if (t[0].kind() == mora::Value::Kind::FormID) c0 = t[0].as_formid();
            else if (t[0].kind() == mora::Value::Kind::Int) c0 = static_cast<uint32_t>(t[0].as_int());
            if (t[1].kind() == mora::Value::Kind::FormID) c1 = t[1].as_formid();
            else if (t[1].kind() == mora::Value::Kind::Int) c1 = static_cast<uint32_t>(t[1].as_int());
            rows.push_back({c0, c1});
        }
        if (rows.empty()) continue;

        arrangements.push_back(mora::emit::build_u32_arrangement(
            static_cast<uint32_t>(i), rows, /*key_col*/ 0));
        arrangements.push_back(mora::emit::build_u32_arrangement(
            static_cast<uint32_t>(i), rows, /*key_col*/ 1));
    }

    if (arrangements.empty()) return {};
    return mora::emit::build_arrangements_section(arrangements);
}

// Write serialized patch table to a binary file
static int write_patch_file(
    mora::PatchBuffer& patch_buf, const std::vector<uint8_t>& string_table,
    const std::string& target_path,
    const std::string& output_dir, mora::Output& out,
    const mora::PluginSet& loaded_plugins,
    const mora::FactDB& facts, mora::StringPool& pool)
{
    fs::path out_path(output_dir);
    if (out_path.is_relative()) {
        fs::path base = fs::path(target_path);
        if (fs::is_regular_file(base)) base = base.parent_path();
        out_path = base / out_path;
    }
    fs::create_directories(out_path);

    // Build a manifest string from the loaded plugin set and hash it so the
    // runtime can detect when the user swaps plugins. PluginSet is an
    // unordered set of filenames; sort for a stable digest.
    std::vector<std::string> plugin_names(loaded_plugins.begin(), loaded_plugins.end());
    std::sort(plugin_names.begin(), plugin_names.end());
    std::string manifest;
    for (const auto& name : plugin_names) {
        manifest += name;
        manifest += '\n';
    }
    auto digest = mora::compute_digest(manifest);

    auto arrangements_section = build_static_arrangements_section(facts, pool);

    out.phase_start("Serializing patches");
    auto patch_data = mora::serialize_patch_table(
        patch_buf.entries(), digest, arrangements_section,
        std::vector<uint8_t>{}, string_table);
    out.phase_done(fmt::format("{} patches \xe2\x86\x92 {}",
        patch_buf.size(), format_bytes(patch_data.size())));

    auto patch_file = out_path / "mora_patches.bin";
    std::ofstream ofs(patch_file, std::ios::binary);
    if (!ofs) {
        out.failure(fmt::format("Failed to open {} for writing", patch_file.string()));
        return 1;
    }
    ofs.write(reinterpret_cast<const char*>(patch_data.data()), patch_data.size());
    ofs.close();

    mora::log::info("  \xe2\x9c\x93 Wrote {}\n", patch_file.string());
    return 0;
}

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

    if (!data_dir.empty()) {
        mora::ext::LoadCtx load_ctx{
            cr.pool,
            cr.diags,
            /*data_dir*/          fs::path(data_dir),
            /*plugins_txt*/       fs::path(plugins_txt),
            /*needed_relations*/  collect_used_relations(cr.modules),
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

    mora::PatchBuffer patch_buf;
    std::vector<uint8_t> string_table;
    mora::ResolvedPatchSet mora_resolved;
    evaluate_mora_rules(cr, evaluator, patch_buf, string_table,
                        mora_resolved, out);
    mora::populate_effect_facts(mora_resolved, db, cr.pool);

    out.phase_done(fmt::format("{} total patches", patch_buf.size()));

    // Write patch file
    int const write_rc = write_patch_file(patch_buf, string_table, target_path, output_dir, out, loaded_plugins, db, cr.pool);
    if (write_rc != 0) return write_rc;

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
    auto patch_path = fs::path(target_path);
    if (fs::is_regular_file(patch_path)) patch_path = patch_path.parent_path();
    patch_path = patch_path / output_dir / "mora_patches.bin";
    auto patch_size = fs::exists(patch_path) ? fs::file_size(patch_path) : 0;

    std::vector<mora::TableRow> summary;
    summary.push_back({"Frozen:", fmt::format("{} rules", static_count),
        fmt::format("\xe2\x86\x92 mora_patches.bin ({})", format_bytes(patch_size))});
    summary.push_back({"", fmt::format("{} patches baked into native code", patch_buf.size()), ""});
    summary.push_back({"", "Estimated runtime: <15ms", ""});
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

    mora::TypeChecker checker(pool, diags, resolver);
    for (auto& mod : modules) checker.check(mod);

    if (diags.has_errors()) {
        mora::DiagRenderer const renderer(use_color);
        mora::log::info("{}", renderer.render_all(diags));
        return 1;
    }

    mora::FactDB const db(pool);
    mora::Evaluator evaluator(pool, diags, db);
    mora::PatchSet all_patches;
    for (auto& mod : modules) {
        auto mod_patches = evaluator.evaluate_static(mod);
        auto resolved = mod_patches.resolve();
        for (auto& rp : resolved.all_patches_sorted()) {
            for (auto& fp : rp.fields) {
                all_patches.add_patch(rp.target_formid, fp.field, fp.op, fp.value, fp.source_mod, fp.priority);
            }
        }
    }

    auto final_resolved = all_patches.resolve();

    if (show_conflicts) {
        const auto& conflicts = final_resolved.get_conflicts();
        if (conflicts.empty()) {
            mora::log::info("  no conflicts\n");
        } else {
            mora::log::info("  {} conflict(s)\n", conflicts.size());
        }
        return 0;
    }

    mora::log::info("  mora inspect \xe2\x80\x94 {} patches (from {} files)\n\n",
        final_resolved.patch_count(), files.size());

    auto all_sorted = final_resolved.all_patches_sorted();
    if (all_sorted.empty()) {
        mora::log::info("  (no patches)\n");
    } else {
        for (auto& rp : all_sorted) {
            mora::log::info("  0x{:08X}:\n", rp.target_formid);
            for (auto& fp : rp.fields) {
                mora::log::info("    {}: {} {}\n", field_name(fp.field), op_prefix(fp.op), format_value(fp.value, pool));
            }
            mora::log::info("\n");
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
                                         "Compile .mora files into mora_patches.bin");
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

        // Zero-arg DX: land mora_patches.bin straight into <Data>/SKSE/Plugins
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
