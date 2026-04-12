#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"
#include "mora/cli/terminal.h"
#include "mora/cli/progress.h"
#include "mora/core/string_pool.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/phase_classifier.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/patch_set.h"
#include "mora/codegen/dll_builder.h"
#include "mora/codegen/address_library.h"
#include "mora/emit/patch_table.h"
#include "mora/esp/load_order.h"
#include "mora/esp/esp_reader.h"
#include "mora/data/schema_registry.h"
#include "mora/import/skypatcher_parser.h"
#include "mora/import/spid_parser.h"
#include "mora/import/kid_parser.h"
#include "mora/import/mora_printer.h"
#include "mora/import/ini_facts.h"
#include "mora/import/ini_distribution_rules.h"
#include "mora/eval/pipeline_evaluator.h"
#include "mora/data/chunk_pool.h"

#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <thread>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

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
        std::fprintf(stderr, "error: could not open file: %s\n", path.string().c_str());
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

static std::vector<fs::path> find_files_by_suffix(const std::string& dir, const std::string& suffix) {
    std::vector<fs::path> files;
    fs::path base(dir);
    if (!fs::exists(base)) return files;
    for (auto& entry : fs::recursive_directory_iterator(base)) {
        if (!entry.is_regular_file()) continue;
        std::string fname = entry.path().filename().string();
        if (fname.size() >= suffix.size() &&
            fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

static std::string format_bytes(std::uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    if (bytes < 1024 * 1024) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (double(bytes) / 1024.0) << " KB";
        return ss.str();
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << (double(bytes) / (1024.0 * 1024.0)) << " MB";
    return ss.str();
}

static fs::path find_address_library(const std::string& data_dir) {
    if (data_dir.empty()) return {};
    fs::path skse_plugins = fs::path(data_dir) / "SKSE" / "Plugins";
    if (!fs::exists(skse_plugins)) return {};

    // Try exact match for 1.6.1170 first (most common AE version)
    auto exact = skse_plugins / "versionlib-1-6-1170-0.bin";
    if (fs::exists(exact)) return exact;

    // Fall back to any versionlib (AE) or version (SE) bin
    for (auto& entry : fs::directory_iterator(skse_plugins)) {
        std::string fname = entry.path().filename().string();
        if ((fname.find("versionlib") == 0 || fname.find("version-") == 0)
            && entry.path().extension() == ".bin") {
            return entry.path();
        }
    }
    return {};
}

static std::string field_name(mora::FieldId id) {
    switch (id) {
        case mora::FieldId::Name:        return "Name";
        case mora::FieldId::Damage:      return "Damage";
        case mora::FieldId::ArmorRating: return "ArmorRating";
        case mora::FieldId::GoldValue:   return "GoldValue";
        case mora::FieldId::Weight:      return "Weight";
        case mora::FieldId::Keywords:    return "Keywords";
        case mora::FieldId::Factions:    return "Factions";
        case mora::FieldId::Perks:       return "Perks";
        case mora::FieldId::Spells:      return "Spells";
        case mora::FieldId::Items:       return "Items";
        case mora::FieldId::Level:       return "Level";
        case mora::FieldId::Race:        return "Race";
        case mora::FieldId::EditorId:    return "EditorId";
        default:                         return "Field(" + std::to_string(static_cast<uint16_t>(id)) + ")";
    }
}

static std::string op_prefix(mora::FieldOp op) {
    switch (op) {
        case mora::FieldOp::Set:    return "set";
        case mora::FieldOp::Add:    return "+";
        case mora::FieldOp::Remove: return "-";
        default:                    return "?";
    }
}

static std::string format_value(const mora::Value& v) {
    switch (v.kind()) {
        case mora::Value::Kind::FormID: {
            std::ostringstream ss;
            ss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << v.as_formid();
            return ss.str();
        }
        case mora::Value::Kind::Int:    return std::to_string(v.as_int());
        case mora::Value::Kind::Float: {
            std::ostringstream ss;
            ss << v.as_float();
            return ss.str();
        }
        case mora::Value::Kind::Bool:   return v.as_bool() ? "true" : "false";
        case mora::Value::Kind::String: return "\"str\"";
        case mora::Value::Kind::Var:    return "?";
        default:                        return "<unknown>";
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
                used.insert(eff.action.index);
            }
            for (auto& ce : rule.conditional_effects) {
                used.insert(ce.effect.action.index);
            }
        }
    }
    return used;
}

static void print_usage() {
    std::printf("Usage: mora <command> [options] [path]\n\n");
    std::printf("Commands:\n");
    std::printf("  check     Type check and lint .mora files\n");
    std::printf("  compile   Compile .mora files to native SKSE DLL\n");
    std::printf("  inspect   Display patch set from .mora source files\n");
    std::printf("  info      Show project status overview\n");
    std::printf("  import    Scan for SPID/KID INI files and display as Mora rules\n");
    std::printf("\nOptions:\n");
    std::printf("  --no-color        Disable colored output\n");
    std::printf("  --output DIR      Output directory (compile, default: MoraCache/)\n");
    std::printf("  --data-dir DIR    Skyrim Data/ directory for ESP loading (compile, info)\n");
    std::printf("  --conflicts       Show only conflict info (inspect)\n");
    std::printf("  -v                Verbose output\n");
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

// Import INI files (SPID + KID) into a synthetic Module.
static mora::Module import_ini_files(
    const std::string& target_path,
    mora::StringPool& pool,
    mora::DiagBag& diags,
    size_t& ini_count)
{
    mora::Module mod;
    mod.filename = "<imported INI rules>";

    auto spid_files = find_files_by_suffix(target_path, "_DISTR.ini");
    auto kid_files  = find_files_by_suffix(target_path, "_KID.ini");

    ini_count = spid_files.size() + kid_files.size();
    if (ini_count == 0) return mod;

    // Parse all INI files in parallel. StringPool and DiagBag are thread-safe,
    // and each parser instance carries no shared state between files.
    std::vector<fs::path> all_ini;
    all_ini.reserve(ini_count);
    all_ini.insert(all_ini.end(), spid_files.begin(), spid_files.end());
    all_ini.insert(all_ini.end(), kid_files.begin(), kid_files.end());

    std::vector<std::future<std::vector<mora::Rule>>> futures;
    futures.reserve(all_ini.size());

    for (auto& p : all_ini) {
        futures.push_back(std::async(std::launch::async,
            [&pool, &diags, path = p.string()]() -> std::vector<mora::Rule> {
                bool is_spid = path.size() >= 10 &&
                    path.compare(path.size() - 10, 10, "_DISTR.ini") == 0;
                if (is_spid) {
                    mora::SpidParser parser(pool, diags);
                    return parser.parse_file(path);
                } else {
                    mora::KidParser parser(pool, diags);
                    return parser.parse_file(path);
                }
            }));
    }

    for (auto& fut : futures) {
        auto rules = fut.get();
        for (auto& r : rules) mod.rules.push_back(std::move(r));
    }

    return mod;
}

static bool run_check_pipeline(
    CheckResult& result,
    const std::string& target_path,
    mora::Progress& progress,
    bool use_color,
    bool import_ini = true)
{
    result.files = find_mora_files(target_path);

    // Parse .mora files
    progress.start_phase("Parsing");
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

    // Import INI files (SPID + KID) directly — only for check/inspect paths.
    // The compile path uses fact-based INI distribution instead.
    size_t ini_count = 0;
    if (import_ini) {
        auto ini_mod = import_ini_files(target_path, result.pool, result.diags, ini_count);
        if (!ini_mod.rules.empty()) {
            result.modules.push_back(std::move(ini_mod));
        }
    }

    if (result.modules.empty() && import_ini) {
        std::fprintf(stderr, "  No .mora or INI files found in %s\n", target_path.c_str());
        return false;
    }

    {
        std::string summary = std::to_string(result.files.size()) + " .mora files";
        if (ini_count > 0) {
            summary += ", " + std::to_string(ini_count) + " INI files";
        }
        progress.finish_phase("Parsing " + summary, "done");
    }

    // Resolve
    progress.start_phase("Resolving");
    mora::NameResolver resolver(result.pool, result.diags);
    for (auto& mod : result.modules) {
        resolver.resolve(mod);
        result.rule_count += mod.rules.size();
    }
    progress.finish_phase(
        "Resolving " + std::to_string(result.rule_count) + " rules",
        "done");

    // Type check
    progress.start_phase("Type checking");
    mora::TypeChecker checker(result.pool, result.diags, resolver);
    for (auto& mod : result.modules) {
        checker.check(mod);
    }
    progress.finish_phase(
        "Type checking " + std::to_string(result.rule_count) + " rules",
        "done");

    // Render diagnostics if any
    if (!result.diags.all().empty()) {
        mora::DiagRenderer renderer(use_color);
        std::printf("\n");
        std::printf("%s", renderer.render_all(result.diags).c_str());
        if (result.diags.error_count() > mora::DiagBag::kMaxErrors) {
            std::printf("\n  ... and %zu more errors (showing first %zu)\n",
                        result.diags.error_count() - mora::DiagBag::kMaxErrors,
                        mora::DiagBag::kMaxErrors);
        }
        if (result.diags.warning_count() > mora::DiagBag::kMaxWarnings) {
            std::printf("\n  ... and %zu more warnings (showing first %zu)\n",
                        result.diags.warning_count() - mora::DiagBag::kMaxWarnings,
                        mora::DiagBag::kMaxWarnings);
        }
    }

    return true;
}

// ── Commands ───────────────────────────────────────────────────────────────

static int cmd_check(const std::string& target_path, mora::Progress& progress, bool use_color) {
    auto start = std::chrono::steady_clock::now();
    progress.print_header("0.1.0");

    CheckResult cr;
    if (!run_check_pipeline(cr, target_path, progress, use_color)) return 1;

    auto total_end = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - start).count();

    if (cr.diags.has_errors()) {
        progress.print_failure("Failed with " + std::to_string(cr.diags.error_count()) +
            " error(s) in " + std::to_string(total_ms) + "ms");
        return 1;
    } else {
        std::string msg = "Checked " + std::to_string(cr.rule_count) + " rules in " +
            std::to_string(total_ms) + "ms";
        if (cr.diags.warning_count() > 0) {
            msg += " (" + std::to_string(cr.diags.warning_count()) + " warnings)";
        }
        progress.print_success(msg);
        return 0;
    }
}

static int cmd_compile(const std::string& target_path, const std::string& output_dir,
                        const std::string& data_dir,
                        mora::Progress& progress, bool use_color) {
    auto start = std::chrono::steady_clock::now();
    progress.print_header("0.1.0");

    CheckResult cr;
    if (!run_check_pipeline(cr, target_path, progress, use_color, /*import_ini=*/false)) return 1;

    // Abort if check found errors
    if (cr.diags.has_errors()) {
        auto total_end = std::chrono::steady_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - start).count();
        progress.print_failure("Failed with " + std::to_string(cr.diags.error_count()) +
            " error(s) in " + std::to_string(total_ms) + "ms — aborting compilation");
        return 1;
    }

    // Phase classification
    progress.start_phase("Classifying");
    mora::PhaseClassifier classifier(cr.pool);
    size_t static_count = 0;
    size_t dynamic_count = 0;
    std::vector<const mora::Rule*> dynamic_rules;

    for (auto& mod : cr.modules) {
        auto classifications = classifier.classify_module(mod);
        for (size_t i = 0; i < classifications.size(); i++) {
            if (classifications[i].phase == mora::Phase::Static) {
                static_count++;
            } else {
                dynamic_count++;
                if (i < mod.rules.size()) {
                    dynamic_rules.push_back(&mod.rules[i]);
                }
            }
        }
    }
    progress.finish_phase(
        std::to_string(static_count) + " static, " + std::to_string(dynamic_count) + " dynamic",
        "done");

    // Evaluation
    progress.start_phase("Evaluating");
    mora::FactDB db(cr.pool);
    mora::Evaluator evaluator(cr.pool, cr.diags, db);

    // If --data-dir provided, load ESP data before evaluation
    mora::EditorIdMap editor_id_map;
    mora::PluginSet loaded_plugins;
    if (!data_dir.empty()) {
        progress.start_phase("Loading ESPs");

        mora::SchemaRegistry schema(cr.pool);
        schema.register_defaults();
        schema.configure_fact_db(db);

        mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);

        // Build PluginSet from load order plugin filenames
        for (auto& p : lo.plugins) {
            loaded_plugins.insert(p.filename().string());
        }

        // Lazy loading: only extract facts that rules actually reference
        auto needed = collect_used_relations(cr.modules);

        // Parallel plugin reading: partition across hardware threads,
        // each with its own EspReader + local FactDB. Merge after.
        auto hw = std::max(1u, std::thread::hardware_concurrency());
        size_t batch_size = (lo.plugins.size() + hw - 1) / hw;

        struct BatchResult {
            mora::FactDB local_db;
            std::unordered_map<std::string, uint32_t> editor_ids;
            BatchResult(mora::StringPool& p) : local_db(p) {}
        };

        std::vector<std::future<BatchResult>> futures;
        for (size_t i = 0; i < lo.plugins.size(); i += batch_size) {
            size_t end = std::min(i + batch_size, lo.plugins.size());
            uint32_t start_idx = static_cast<uint32_t>(i);

            futures.push_back(std::async(std::launch::async,
                [&, start_idx, i, end]() -> BatchResult {
                    BatchResult result(cr.pool);
                    schema.configure_fact_db(result.local_db);

                    mora::EspReader reader(cr.pool, cr.diags, schema);
                    reader.set_needed_relations(needed);

                    // Read this batch's plugins sequentially into local DB
                    std::vector<std::filesystem::path> batch(
                        lo.plugins.begin() + i, lo.plugins.begin() + end);
                    reader.read_load_order(batch, result.local_db);
                    result.editor_ids = reader.editor_id_map();
                    return result;
                }));
        }

        // Merge results
        for (auto& fut : futures) {
            auto result = fut.get();
            db.merge_from(result.local_db);
            for (auto& [edid, formid] : result.editor_ids) {
                editor_id_map[edid] = formid;
            }
        }

        // Wire up symbol resolution to the evaluator
        for (auto& [edid, formid] : editor_id_map) {
            evaluator.set_symbol_formid(cr.pool.intern(edid), formid);
        }

        progress.finish_phase(
            std::to_string(lo.plugins.size()) + " plugins, " +
            std::to_string(needed.size()) + " relations → " +
            std::to_string(db.fact_count()) + " facts", "done");
    }

    // Load INI distribution facts (with FormID resolution from ESPs)
    mora::configure_ini_relations(db, cr.pool);
    uint32_t next_rule_id = 1;

    auto spid_files = find_files_by_suffix(target_path, "_DISTR.ini");
    auto kid_files = find_files_by_suffix(target_path, "_KID.ini");

    mora::IniLoadStats ini_stats;

    if (!spid_files.empty() || !kid_files.empty()) {
        progress.start_phase("Loading INI distributions");
        size_t spid_count = 0, kid_count = 0;
        for (auto& path : spid_files) {
            spid_count += mora::emit_spid_facts(path.string(), db, cr.pool,
                                                 cr.diags, next_rule_id, editor_id_map,
                                                 loaded_plugins, ini_stats);
        }
        for (auto& path : kid_files) {
            kid_count += mora::emit_kid_facts(path.string(), db, cr.pool,
                                               cr.diags, next_rule_id, editor_id_map,
                                               loaded_plugins, ini_stats);
        }
        progress.finish_phase(
            std::to_string(spid_count) + " SPID + " + std::to_string(kid_count) + " KID → " +
            std::to_string(next_rule_id - 1) + " distribution facts", "done");

        // Report INI loading stats
        if (ini_stats.rules_dropped_missing_plugin > 0) {
            std::printf("    Dropped %zu rules referencing %zu missing plugins\n",
                        ini_stats.rules_dropped_missing_plugin,
                        ini_stats.missing_plugins.size());
            // Show top 5 missing plugins by count
            std::vector<std::pair<std::string, size_t>> sorted_missing(
                ini_stats.missing_plugins.begin(), ini_stats.missing_plugins.end());
            std::sort(sorted_missing.begin(), sorted_missing.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            size_t show = std::min(sorted_missing.size(), size_t(5));
            for (size_t i = 0; i < show; ++i) {
                std::printf("      %s (%zu rules)\n",
                            sorted_missing[i].first.c_str(),
                            sorted_missing[i].second);
            }
        }
        if (ini_stats.unresolved_editor_ids > 0) {
            std::printf("    %zu editor IDs could not be resolved\n",
                        ini_stats.unresolved_editor_ids);
        }

        // (Old Datalog distribution rules no longer added — columnar path handles them)
    }

    mora::PatchSet all_patches;
    mora::PatchBuffer patch_buf;

    // ── Columnar evaluation for INI distributions (fast path) ──
    if (next_rule_id > 1) {
        mora::ChunkPool chunk_pool;
        mora::ColumnarFactStore col_store(chunk_pool);

        // Copy ESP relations into columnar format
        auto copy_relation = [&](const char* name, std::vector<mora::ColType> types) {
            auto rel_id = cr.pool.intern(name);
            auto& old_rel = db.get_relation(rel_id);
            if (old_rel.empty()) return;
            auto& col_rel = col_store.get_or_create(rel_id, types);
            for (auto& tuple : old_rel) {
                uint64_t vals[8];
                for (size_t i = 0; i < tuple.size() && i < 8; i++) {
                    switch (tuple[i].kind()) {
                        case mora::Value::Kind::FormID: vals[i] = tuple[i].as_formid(); break;
                        case mora::Value::Kind::Int:    vals[i] = static_cast<uint64_t>(tuple[i].as_int()); break;
                        case mora::Value::Kind::String: vals[i] = tuple[i].as_string().index; break;
                        default: vals[i] = 0; break;
                    }
                }
                col_rel.append_row(vals);
            }
        };

        copy_relation("npc", {mora::ColType::U32});
        copy_relation("weapon", {mora::ColType::U32});
        copy_relation("armor", {mora::ColType::U32});
        copy_relation("ammo", {mora::ColType::U32});
        copy_relation("potion", {mora::ColType::U32});
        copy_relation("book", {mora::ColType::U32});
        copy_relation("spell", {mora::ColType::U32});
        copy_relation("misc_item", {mora::ColType::U32});
        copy_relation("magic_effect", {mora::ColType::U32});
        copy_relation("ingredient", {mora::ColType::U32});
        copy_relation("scroll", {mora::ColType::U32});
        copy_relation("soul_gem", {mora::ColType::U32});
        copy_relation("has_keyword", {mora::ColType::U32, mora::ColType::U32});
        copy_relation("race_of", {mora::ColType::U32, mora::ColType::U32});
        copy_relation("has_faction", {mora::ColType::U32, mora::ColType::U32});

        // Expand filter lists into flat columnar relations
        auto expand_filters = [&](const char* filter_rel_name,
                                   const char* kw_out_name,
                                   const char* form_out_name,
                                   const char* none_out_name) {
            auto filter_id = cr.pool.intern(filter_rel_name);
            auto& filter_tuples = db.get_relation(filter_id);
            auto keyword_sid = cr.pool.intern("keyword");
            auto none_sid = cr.pool.intern("none");

            auto& kw_out = col_store.get_or_create(cr.pool.intern(kw_out_name),
                                                     {mora::ColType::U32, mora::ColType::U32});
            auto& form_out = col_store.get_or_create(cr.pool.intern(form_out_name),
                                                       {mora::ColType::U32, mora::ColType::U32});
            auto& none_out = col_store.get_or_create(cr.pool.intern(none_out_name),
                                                       {mora::ColType::U32});

            for (auto& tuple : filter_tuples) {
                uint32_t rule_id = static_cast<uint32_t>(tuple[0].as_int());
                auto filter_kind = tuple[1].as_string();

                if (filter_kind == none_sid) {
                    uint64_t v = rule_id;
                    none_out.append_row(&v);
                } else if (tuple.size() > 2 && tuple[2].kind() == mora::Value::Kind::List) {
                    auto& list = tuple[2].as_list();
                    bool is_keyword = (filter_kind == keyword_sid);
                    auto& target = is_keyword ? kw_out : form_out;
                    for (auto& item : list) {
                        if (item.kind() == mora::Value::Kind::FormID) {
                            uint64_t vals[2] = {rule_id, item.as_formid()};
                            target.append_row(vals);
                        }
                    }
                }
            }
        };

        expand_filters("spid_filter", "spid_kw_filter", "spid_form_filter", "spid_no_filter");
        expand_filters("kid_filter", "kid_kw_filter", "kid_form_filter", "kid_no_filter");

        // Copy distribution relations
        copy_relation("spid_dist", {mora::ColType::U32, mora::ColType::U32, mora::ColType::U32});
        copy_relation("kid_dist", {mora::ColType::U32, mora::ColType::U32, mora::ColType::U32});

        col_store.build_all_indexes();

        progress.start_phase("Evaluating (columnar)");
        mora::evaluate_distributions_columnar(col_store, cr.pool, patch_buf);
        patch_buf.sort_and_dedup();
        progress.finish_phase(
            std::to_string(patch_buf.size()) + " patches generated", "done");
    }

    // ── Datalog evaluation for .mora file rules (if any) ──
    bool is_tty = mora::stdout_is_tty();
    if (!cr.modules.empty()) {
        progress.start_phase("Evaluating (.mora rules)");
        auto eval_progress = [&](size_t current, size_t total, std::string_view name) {
            if (is_tty) {
                std::fprintf(stderr, "\r  Evaluating rule %zu / %zu ...%60s", current, total, "");
            }
        };

        for (auto& mod : cr.modules) {
            auto mod_patches = evaluator.evaluate_static(mod, eval_progress);
            auto resolved = mod_patches.resolve();
            for (auto& rp : resolved.all_patches_sorted()) {
                for (auto& fp : rp.fields) {
                    all_patches.add_patch(rp.target_formid, fp.field, fp.op, fp.value, fp.source_mod, fp.priority);
                }
            }
        }
        if (is_tty) std::fprintf(stderr, "\r%60s\r", ""); // clear progress line
        progress.finish_phase("done", "done");
    }

    // Merge .mora patches (if any) into the PatchBuffer
    if (!cr.modules.empty()) {
        auto mora_resolved = all_patches.resolve();
        for (auto& rp : mora_resolved.all_patches_sorted()) {
            for (auto& fp : rp.fields) {
                uint8_t vtype = 0;
                uint64_t val = 0;
                switch (fp.value.kind()) {
                    case mora::Value::Kind::FormID:
                        vtype = static_cast<uint8_t>(mora::PatchValueType::FormID);
                        val = fp.value.as_formid();
                        break;
                    case mora::Value::Kind::Int:
                        vtype = static_cast<uint8_t>(mora::PatchValueType::Int);
                        std::memcpy(&val, &fp.value, 8); // safe: int fits in 8 bytes
                        val = static_cast<uint64_t>(fp.value.as_int());
                        break;
                    case mora::Value::Kind::Float: {
                        vtype = static_cast<uint8_t>(mora::PatchValueType::Float);
                        double d = fp.value.as_float();
                        std::memcpy(&val, &d, 8);
                        break;
                    }
                    default:
                        continue; // skip unsupported value types
                }
                patch_buf.emit(rp.target_formid,
                               static_cast<uint8_t>(fp.field),
                               static_cast<uint8_t>(fp.op),
                               vtype, val);
            }
        }
        // Re-sort after merging .mora patches
        patch_buf.sort_and_dedup();
    }

    progress.finish_phase(
        std::to_string(patch_buf.size()) + " total patches",
        "done");

    // ── LLVM Codegen ──

    // Determine output directory — resolve relative to target
    fs::path out_path(output_dir);
    if (out_path.is_relative()) {
        fs::path base = fs::path(target_path);
        if (fs::is_regular_file(base)) {
            base = base.parent_path();
        }
        out_path = base / out_path;
    }
    fs::create_directories(out_path);

    progress.start_phase("Loading Address Library");
    mora::AddressLibrary addrlib;
    // Try to find real Address Library
    auto addr_lib_path = find_address_library(data_dir);
    if (!addr_lib_path.empty()) {
        addrlib.load(addr_lib_path);
    } else {
        // Use mock with known SE 1.5.97 offset
        addrlib = mora::AddressLibrary::mock({{514351, 0x1EEBE10}});
        if (!data_dir.empty()) {
            std::printf("  \xe2\x9a\xa0 No Address Library found \xe2\x80\x94 using fallback offsets (SE 1.5.97)\n");
        }
    }
    progress.finish_phase(std::to_string(addrlib.entry_count()) + " entries", "done");

    mora::DLLBuilder builder(addrlib);

    // Find mora_rt.lib — look next to the mora binary, then in data/
    fs::path rt_lib_path;
    {
        auto exe_dir = fs::canonical("/proc/self/exe").parent_path();
        std::vector<fs::path> candidates = {
            exe_dir / "mora_rt.lib",
            exe_dir / "../data/mora_rt.lib",
            exe_dir / "../../data/mora_rt.lib",
            exe_dir / "../../../data/mora_rt.lib",
            exe_dir / "../../../../data/mora_rt.lib",
            fs::path("data/mora_rt.lib"),
        };
        for (auto& p : candidates) {
            if (fs::exists(p)) { rt_lib_path = fs::canonical(p); break; }
        }
    }

    progress.start_phase("Serializing patches");
    auto patch_data = mora::serialize_patch_table(patch_buf.entries(), addrlib);
    progress.finish_phase(
        std::to_string(patch_buf.size()) + " patches \xe2\x86\x92 " +
        format_bytes(patch_data.size()), "done");

    auto build_result = builder.build_data_dll(patch_data, patch_buf.size(),
                                                out_path, rt_lib_path);
    if (!build_result.success) {
        progress.print_failure("DLL build failed: " + build_result.error);
        return 1;
    }

    // Show codegen phase breakdown using pre-recorded timings from build()
    auto fmt_ms = [](double ms) -> std::string {
        if (ms < 1.0) return "<1ms";
        if (ms < 1000.0) return std::to_string(static_cast<int>(ms)) + "ms";
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (ms / 1000.0) << "s";
        return ss.str();
    };
    std::printf("  \xe2\x9c\x93 Data IR (%s) %.*s%s\n",
                format_bytes(patch_data.size()).c_str(),
                46, ". . . . . . . . . . . . . . . . . . . . . . .",
                fmt_ms(build_result.ir_gen_ms).c_str());
    std::printf("  \xe2\x9c\x93 Compile to x86-64 COFF %.*s%s\n",
                40, ". . . . . . . . . . . . . . . . . . . .",
                fmt_ms(build_result.compile_ms).c_str());
    if (!fs::exists(build_result.output_path)) {
        progress.print_failure("DLL output missing: " + build_result.output_path.string());
        return 1;
    }
    std::printf("  ✓ Link MoraRuntime.dll (%s) %.*s%s\n",
                format_bytes(fs::file_size(build_result.output_path)).c_str(),
                34, ". . . . . . . . . . . . . . . . .",
                fmt_ms(build_result.link_ms).c_str());

    // Summary
    auto dll_size = fs::file_size(build_result.output_path);
    progress.print_summary(
        static_count,
        patch_buf.size(),
        dynamic_count,
        0u,  // conflict detection not available with PatchBuffer fast path
        cr.diags.error_count(),
        cr.diags.warning_count(),
        format_bytes(dll_size));

    auto total_end = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - start).count();

    std::string msg = "Compiled " + std::to_string(cr.rule_count) + " rules in " +
        std::to_string(total_ms) + "ms";
    if (cr.diags.warning_count() > 0) {
        msg += " (" + std::to_string(cr.diags.warning_count()) + " warnings)";
    }
    progress.print_success(msg);
    return 0;
}

static int cmd_inspect(const std::string& target_path, bool show_conflicts,
                       bool use_color) {
    // Inspect now works by compiling in-memory and displaying the PatchSet
    mora::Progress progress(use_color, mora::stdout_is_tty());

    auto files = find_mora_files(target_path);
    if (files.empty()) {
        std::fprintf(stderr, "  No .mora files found in %s\n", target_path.c_str());
        std::fprintf(stderr, "  hint: mora inspect reads .mora source files and displays the patch set\n");
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
        mora::DiagRenderer renderer(use_color);
        std::printf("%s", renderer.render_all(diags).c_str());
        return 1;
    }

    mora::FactDB db(pool);
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
        auto conflicts = final_resolved.get_conflicts();
        if (conflicts.empty()) {
            std::printf("  no conflicts\n");
        } else {
            std::printf("  %zu conflict(s)\n", conflicts.size());
        }
        return 0;
    }

    // Header
    std::printf("  mora inspect — %zu patches (from %zu files)\n\n",
        final_resolved.patch_count(), files.size());

    auto all_sorted = final_resolved.all_patches_sorted();
    if (all_sorted.empty()) {
        std::printf("  (no patches)\n");
    } else {
        for (auto& rp : all_sorted) {
            std::printf("  0x%08X:\n", rp.target_formid);
            for (auto& fp : rp.fields) {
                std::string op = op_prefix(fp.op);
                std::string val = format_value(fp.value);
                std::printf("    %s: %s %s\n", field_name(fp.field).c_str(), op.c_str(), val.c_str());
            }
            std::printf("\n");
        }
    }

    return 0;
}

static int cmd_info(const std::string& target_path, const std::string& data_dir) {
    std::printf("  mora v0.1.0\n\n");

    // Find and count .mora files/rules
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

    std::printf("  Mora rules:    %zu across %zu files\n", rule_count, files.size());

    // Check cache — look for compiled DLL
    fs::path cache_dir = base / "MoraCache";
    fs::path dll_path = cache_dir / "MoraRuntime.dll";

    if (!fs::exists(cache_dir) || !fs::exists(dll_path)) {
        std::printf("  Cache status:  no DLL (run mora compile)\n");
    } else {
        auto dll_size = fs::file_size(dll_path);
        std::printf("  Cache status:  MoraRuntime.dll (%s)\n", format_bytes(dll_size).c_str());
    }

    // If --data-dir provided, show plugin count and fact count
    if (!data_dir.empty()) {
        mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);
        std::printf("  Data dir:      %s\n", data_dir.c_str());
        std::printf("  Plugins found: %zu\n", lo.plugins.size());

        if (!lo.plugins.empty()) {
            mora::FactDB db(pool);
            mora::SchemaRegistry schema(pool);
            schema.register_defaults();
            schema.configure_fact_db(db);
            mora::EspReader esp_reader(pool, diags, schema);
            esp_reader.read_load_order(lo.plugins, db);
            std::printf("  Facts loaded:  %zu\n", db.fact_count());
        }
    }

    return 0;
}

static void print_file_header(const std::string& filename, size_t rule_count, bool use_color) {
    std::string title = " " + filename + " (" + std::to_string(rule_count) + " rules) ";
    size_t total_width = 54;
    size_t fill = (total_width > title.size() + 4) ? total_width - title.size() - 4 : 0;
    std::string fill_str;
    for (size_t i = 0; i < fill; ++i) fill_str += "\u2500";
    std::string header = "  " + mora::TermStyle::cyan("\u2500\u2500" + title + fill_str, use_color);
    std::printf("%s\n\n", header.c_str());
}

static int cmd_import(const std::string& target_path, const std::string& data_dir,
                      bool use_color) {
    mora::Progress progress(use_color, mora::stdout_is_tty());
    progress.print_header("0.1.0");

    auto spid_files = find_files_by_suffix(target_path, "_DISTR.ini");
    auto kid_files  = find_files_by_suffix(target_path, "_KID.ini");

    // Look for SkyPatcher directory
    fs::path skypatcher_dir;
    for (auto& candidate : {
        fs::path(target_path) / "SKSE" / "Plugins" / "SkyPatcher",
        fs::path(target_path) / "SkyPatcher",
        fs::path(target_path),  // might be the SkyPatcher dir itself
    }) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            // Check if it has subdirectories named after record types
            for (auto& sub : fs::directory_iterator(candidate)) {
                auto name = sub.path().filename().string();
                if (name == "weapon" || name == "armor" || name == "npc" ||
                    name == "leveledList" || name == "magicEffect") {
                    skypatcher_dir = candidate;
                    break;
                }
            }
            if (!skypatcher_dir.empty()) break;
        }
    }

    if (spid_files.empty() && kid_files.empty() && skypatcher_dir.empty()) {
        std::printf("  No INI files found in %s\n", target_path.c_str());
        return 1;
    }

    mora::StringPool pool;
    mora::DiagBag diags;

    // Load ESP data for FormID resolution (optional but improves output)
    mora::FormIdResolver resolver;
    if (!data_dir.empty()) {
        progress.start_phase("Loading ESP data for symbol resolution");
        mora::SchemaRegistry schema(pool);
        schema.register_defaults();
        mora::FactDB db(pool);
        schema.configure_fact_db(db);
        mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);
        mora::EspReader esp_reader(pool, diags, schema);
        esp_reader.read_load_order(lo.plugins, db);
        resolver.build_from_editor_ids(esp_reader.editor_id_map());
        progress.finish_phase(
            std::to_string(resolver.has_data() ? esp_reader.editor_id_map().size() : 0)
            + " EditorIDs loaded", "done");
        std::printf("\n");
    }

    std::printf("  Scanning for INI files...\n\n");

    mora::MoraPrinter printer(pool);
    const mora::FormIdResolver* resolver_ptr = resolver.has_data() ? &resolver : nullptr;

    size_t total_spid_rules = 0;
    size_t total_kid_rules  = 0;

    // Process SPID files
    for (auto& path : spid_files) {
        mora::SpidParser parser(pool, diags, resolver_ptr);
        auto rules = parser.parse_file(path.string());

        std::string fname = path.filename().string();
        print_file_header(fname, rules.size(), use_color);

        for (auto& rule : rules) {
            // Source comment
            std::string src_file = rule.span.file.empty() ? fname : rule.span.file;
            // Strip directory from src_file for display
            {
                auto pos = src_file.rfind('/');
                if (pos != std::string::npos) src_file = src_file.substr(pos + 1);
            }
            uint32_t line_num = rule.span.start_line;
            std::string comment = "Imported from " + src_file + ":" + std::to_string(line_num);
            std::printf("  %s\n", mora::TermStyle::dim(printer.print_comment(comment), use_color).c_str());

            // Rule text with coloring
            std::string rule_text = printer.print_rule(rule);
            // Print rule name line in bold, rest normally, symbols in cyan
            std::istringstream ss(rule_text);
            std::string line;
            bool first_line = true;
            while (std::getline(ss, line)) {
                if (first_line) {
                    // Bold rule name
                    std::printf("  %s\n", mora::TermStyle::bold(line, use_color).c_str());
                    first_line = false;
                } else if (!line.empty()) {
                    // Colorize :Symbol tokens in cyan
                    if (use_color) {
                        std::string colored;
                        size_t i = 0;
                        while (i < line.size()) {
                            if (line[i] == ':' && i + 1 < line.size() &&
                                (std::isalpha((unsigned char)line[i+1]) || line[i+1] == '_')) {
                                // find end of symbol
                                size_t j = i + 1;
                                while (j < line.size() &&
                                       (std::isalnum((unsigned char)line[j]) || line[j] == '_')) {
                                    ++j;
                                }
                                colored += mora::TermStyle::cyan(line.substr(i, j - i), true);
                                i = j;
                            } else {
                                colored += line[i++];
                            }
                        }
                        std::printf("  %s\n", colored.c_str());
                    } else {
                        std::printf("  %s\n", line.c_str());
                    }
                } else {
                    std::printf("\n");
                }
            }
            std::printf("\n");
        }

        total_spid_rules += rules.size();
    }

    // Process KID files
    for (auto& path : kid_files) {
        mora::KidParser parser(pool, diags, resolver_ptr);
        auto rules = parser.parse_file(path.string());

        std::string fname = path.filename().string();
        print_file_header(fname, rules.size(), use_color);

        for (auto& rule : rules) {
            std::string src_file = rule.span.file.empty() ? fname : rule.span.file;
            {
                auto pos = src_file.rfind('/');
                if (pos != std::string::npos) src_file = src_file.substr(pos + 1);
            }
            uint32_t line_num = rule.span.start_line;
            std::string comment = "Imported from " + src_file + ":" + std::to_string(line_num);
            std::printf("  %s\n", mora::TermStyle::dim(printer.print_comment(comment), use_color).c_str());

            std::string rule_text = printer.print_rule(rule);
            std::istringstream ss(rule_text);
            std::string line;
            bool first_line = true;
            while (std::getline(ss, line)) {
                if (first_line) {
                    std::printf("  %s\n", mora::TermStyle::bold(line, use_color).c_str());
                    first_line = false;
                } else if (!line.empty()) {
                    if (use_color) {
                        std::string colored;
                        size_t i = 0;
                        while (i < line.size()) {
                            if (line[i] == ':' && i + 1 < line.size() &&
                                (std::isalpha((unsigned char)line[i+1]) || line[i+1] == '_')) {
                                size_t j = i + 1;
                                while (j < line.size() &&
                                       (std::isalnum((unsigned char)line[j]) || line[j] == '_')) {
                                    ++j;
                                }
                                colored += mora::TermStyle::cyan(line.substr(i, j - i), true);
                                i = j;
                            } else {
                                colored += line[i++];
                            }
                        }
                        std::printf("  %s\n", colored.c_str());
                    } else {
                        std::printf("  %s\n", line.c_str());
                    }
                } else {
                    std::printf("\n");
                }
            }
            std::printf("\n");
        }

        total_kid_rules += rules.size();
    }

    // Process SkyPatcher directories
    size_t total_skypatcher_rules = 0;
    size_t skypatcher_file_count = 0;
    if (!skypatcher_dir.empty()) {
        mora::SkyPatcherParser sky_parser(pool, diags, resolver_ptr);
        auto sky_rules = sky_parser.parse_directory(skypatcher_dir.string());

        if (!sky_rules.empty()) {
            print_file_header("SkyPatcher (" + skypatcher_dir.string() + ")",
                              sky_rules.size(), use_color);

            for (auto& rule : sky_rules) {
                std::string rule_text = printer.print_rule(rule);
                std::istringstream ss(rule_text);
                std::string line;
                bool first_line = true;
                while (std::getline(ss, line)) {
                    if (first_line) {
                        std::printf("  %s\n", mora::TermStyle::bold(line, use_color).c_str());
                        first_line = false;
                    } else if (!line.empty()) {
                        std::printf("  %s\n", line.c_str());
                    } else {
                        std::printf("\n");
                    }
                }
                std::printf("\n");
            }

            total_skypatcher_rules = sky_rules.size();
        }

        // Count files
        for (auto& entry : fs::recursive_directory_iterator(skypatcher_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ini")
                skypatcher_file_count++;
        }
    }

    // Summary
    size_t total_rules = total_spid_rules + total_kid_rules + total_skypatcher_rules;
    size_t total_files = spid_files.size() + kid_files.size() + skypatcher_file_count;

    std::string summary_header = "\u2500\u2500 Summary " + std::string(43, '\0');
    std::string fill_str;
    for (size_t i = 0; i < 43; ++i) fill_str += "\u2500";
    std::printf("  %s\n", mora::TermStyle::cyan("\u2500\u2500 Summary " + fill_str, use_color).c_str());

    std::printf("  %s INI files \u2192 %s Mora rules\n",
        mora::TermStyle::green(std::to_string(total_files), use_color).c_str(),
        mora::TermStyle::green(std::to_string(total_rules), use_color).c_str());

    if (!spid_files.empty()) {
        std::printf("    SPID: %s file%s, %s rule%s\n",
            mora::TermStyle::green(std::to_string(spid_files.size()), use_color).c_str(),
            spid_files.size() == 1 ? "" : "s",
            mora::TermStyle::green(std::to_string(total_spid_rules), use_color).c_str(),
            total_spid_rules == 1 ? "" : "s");
    }
    if (!kid_files.empty()) {
        std::printf("    KID:  %s file%s, %s rule%s\n",
            mora::TermStyle::green(std::to_string(kid_files.size()), use_color).c_str(),
            kid_files.size() == 1 ? "" : "s",
            mora::TermStyle::green(std::to_string(total_kid_rules), use_color).c_str(),
            total_kid_rules == 1 ? "" : "s");
    }
    if (total_skypatcher_rules > 0) {
        std::printf("    SkyPatcher: %s file%s, %s rule%s\n",
            mora::TermStyle::green(std::to_string(skypatcher_file_count), use_color).c_str(),
            skypatcher_file_count == 1 ? "" : "s",
            mora::TermStyle::green(std::to_string(total_skypatcher_rules), use_color).c_str(),
            total_skypatcher_rules == 1 ? "" : "s");
    }
    std::printf("\n");

    return 0;
}

// ── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    std::string command = argv[1];
    bool force_no_color = false;
    bool show_conflicts = false;
    std::string target_path = ".";
    std::string output_dir = "MoraCache";
    std::string data_dir;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-color") force_no_color = true;
        else if (arg == "--conflicts") show_conflicts = true;
        else if (arg == "--output" && i + 1 < argc) { output_dir = argv[++i]; }
        else if (arg == "--data-dir" && i + 1 < argc) { data_dir = argv[++i]; }
        else target_path = arg;
    }

    // Auto-detect Skyrim Data directory if not specified
    if (data_dir.empty() && (command == "compile" || command == "info" || command == "import")) {
        data_dir = detect_skyrim_data_dir();
    }

    bool use_color = !force_no_color && mora::color_enabled();
    bool is_tty = mora::stdout_is_tty();
    mora::Progress progress(use_color, is_tty);

    if (command == "check") {
        return cmd_check(target_path, progress, use_color);
    }

    if (command == "compile") {
        return cmd_compile(target_path, output_dir, data_dir, progress, use_color);
    }

    if (command == "inspect") {
        return cmd_inspect(target_path, show_conflicts, use_color);
    }

    if (command == "info") {
        return cmd_info(target_path, data_dir);
    }

    if (command == "import") {
        return cmd_import(target_path, data_dir, use_color);
    }

    print_usage();
    return 1;
}
