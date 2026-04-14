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
#include "mora/data/action_names.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/phase_classifier.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/patch_set.h"
#include "mora/emit/patch_table.h"
#include "mora/emit/arrangement_emit.h"
#include "mora/dag/compile.h"
#include "mora/dag/bytecode.h"
#include "mora/model/relations.h"
#include "mora/core/digest.h"
#include <algorithm>
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

#include <fmt/format.h>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

// Forward declaration (defined in cli/doc_generator.cpp)
namespace mora { void generate_docs(std::ostream& out); }

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
    if (bytes < 1024) return fmt::format("{} bytes", bytes);
    if (bytes < 1024 * 1024) return fmt::format("{:.1f} KB", double(bytes) / 1024.0);
    return fmt::format("{:.1f} MB", double(bytes) / (1024.0 * 1024.0));
}

// (address library lookup removed — no longer needed without LLVM codegen)

static std::string field_name(mora::FieldId id) {
    switch (id) {
        case mora::FieldId::Name:            return "Name";
        case mora::FieldId::Damage:          return "Damage";
        case mora::FieldId::ArmorRating:     return "ArmorRating";
        case mora::FieldId::GoldValue:       return "GoldValue";
        case mora::FieldId::Weight:          return "Weight";
        case mora::FieldId::Keywords:        return "Keywords";
        case mora::FieldId::Factions:        return "Factions";
        case mora::FieldId::Perks:           return "Perks";
        case mora::FieldId::Spells:          return "Spells";
        case mora::FieldId::Items:           return "Items";
        case mora::FieldId::Level:           return "Level";
        case mora::FieldId::Race:            return "Race";
        case mora::FieldId::EditorId:        return "EditorId";
        case mora::FieldId::Shouts:          return "Shouts";
        case mora::FieldId::LevSpells:       return "LevSpells";
        case mora::FieldId::Speed:           return "Speed";
        case mora::FieldId::Reach:           return "Reach";
        case mora::FieldId::Stagger:         return "Stagger";
        case mora::FieldId::RangeMin:        return "RangeMin";
        case mora::FieldId::RangeMax:        return "RangeMax";
        case mora::FieldId::CritDamage:      return "CritDamage";
        case mora::FieldId::CritPercent:     return "CritPercent";
        case mora::FieldId::Health:          return "Health";
        case mora::FieldId::CalcLevelMin:    return "CalcLevelMin";
        case mora::FieldId::CalcLevelMax:    return "CalcLevelMax";
        case mora::FieldId::SpeedMult:       return "SpeedMult";
        case mora::FieldId::RaceForm:        return "Race(form)";
        case mora::FieldId::ClassForm:       return "Class";
        case mora::FieldId::SkinForm:        return "Skin";
        case mora::FieldId::OutfitForm:      return "Outfit";
        case mora::FieldId::EnchantmentForm: return "Enchantment";
        case mora::FieldId::VoiceTypeForm:   return "VoiceType";
        case mora::FieldId::LeveledEntries:  return "LeveledEntries";
        case mora::FieldId::ClearAll:        return "ClearAll";
        case mora::FieldId::AutoCalcStats:   return "AutoCalcStats";
        case mora::FieldId::Essential:       return "Essential";
        case mora::FieldId::Protected:       return "Protected";
        default:                             return fmt::format("Field({})", static_cast<uint16_t>(id));
    }
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

static std::string format_value(const mora::Value& v) {
    switch (v.kind()) {
        case mora::Value::Kind::FormID: return fmt::format("0x{:08X}", v.as_formid());
        case mora::Value::Kind::Int:    return fmt::format("{}", v.as_int());
        case mora::Value::Kind::Float:  return fmt::format("{}", v.as_float());
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
                used.insert(eff.name.index);
            }
            for (auto& ce : rule.conditional_effects) {
                used.insert(ce.effect.name.index);
            }
        }
    }
    return used;
}

static void print_usage() {
    mora::log::info(
        "Usage: mora <command> [options] [path]\n\n"
        "Commands:\n"
        "  check     Type check and lint .mora files\n"
        "  compile   Compile .mora files to native SKSE DLL\n"
        "  inspect   Display patch set from .mora source files\n"
        "  info      Show project status overview\n"
        "  import    Scan for SPID/KID INI files and display as Mora rules\n"
        "\nOptions:\n"
        "  --no-color        Disable colored output\n"
        "  --output DIR      Output directory (compile, default: MoraCache/)\n"
        "  --data-dir DIR    Skyrim Data/ directory for ESP loading (compile, info)\n"
        "  --conflicts       Show only conflict info (inspect)\n"
        "  -v                Verbose output\n");
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
    mora::Output& out,
    bool use_color,
    bool import_ini = true)
{
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
        mora::log::error("No .mora or INI files found in {}\n", target_path);
        return false;
    }

    {
        auto summary = fmt::format("{} .mora files", result.files.size());
        if (ini_count > 0) {
            summary += fmt::format(", {} INI files", ini_count);
        }
        out.phase_done(fmt::format("Parsing {}", summary));
    }

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
        mora::DiagRenderer renderer(use_color);
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
    out.print_header("0.1.0");

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

// Convert a Value to a uint64_t for patch table encoding
static uint64_t value_to_u64(const mora::Value& v) {
    switch (v.kind()) {
        case mora::Value::Kind::FormID: return v.as_formid();
        case mora::Value::Kind::Int:    return static_cast<uint64_t>(v.as_int());
        case mora::Value::Kind::String: return v.as_string().index;
        default: return 0;
    }
}

// Load ESP plugin data into the FactDB via parallel batched reading
static void load_esp_data(
    CheckResult& cr, mora::FactDB& db, mora::Evaluator& evaluator,
    const std::string& data_dir, mora::Output& out,
    mora::EditorIdMap& editor_id_map, mora::PluginSet& loaded_plugins)
{
    out.phase_start("Loading ESPs");

    mora::SchemaRegistry schema(cr.pool);
    schema.register_defaults();
    schema.configure_fact_db(db);

    mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);
    for (auto& p : lo.plugins) {
        loaded_plugins.insert(p.filename().string());
    }

    auto needed = collect_used_relations(cr.modules);

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
        futures.push_back(std::async(std::launch::async,
            [&, i, end]() -> BatchResult {
                BatchResult result(cr.pool);
                schema.configure_fact_db(result.local_db);
                mora::EspReader reader(cr.pool, cr.diags, schema);
                reader.set_needed_relations(needed);
                std::vector<std::filesystem::path> batch(
                    lo.plugins.begin() + i, lo.plugins.begin() + end);
                reader.read_load_order(batch, result.local_db);
                result.editor_ids = reader.editor_id_map();
                return result;
            }));
    }

    for (auto& fut : futures) {
        auto result = fut.get();
        db.merge_from(result.local_db);
        for (auto& [edid, formid] : result.editor_ids) {
            editor_id_map[edid] = formid;
        }
    }

    for (auto& [edid, formid] : editor_id_map) {
        evaluator.set_symbol_formid(cr.pool.intern(edid), formid);
    }

    out.phase_done(fmt::format("{} plugins, {} relations \xe2\x86\x92 {} facts",
        lo.plugins.size(), needed.size(), db.fact_count()));
}

// Load and process INI distribution files into FactDB
static uint32_t load_ini_distributions(
    const std::string& target_path, CheckResult& cr, mora::FactDB& db,
    mora::Output& out, const mora::EditorIdMap& editor_id_map,
    const mora::PluginSet& loaded_plugins)
{
    mora::configure_ini_relations(db, cr.pool);
    uint32_t next_rule_id = 1;

    auto spid_files = find_files_by_suffix(target_path, "_DISTR.ini");
    auto kid_files = find_files_by_suffix(target_path, "_KID.ini");

    if (spid_files.empty() && kid_files.empty()) return 0;

    mora::IniLoadStats ini_stats;
    out.phase_start("Loading INI distributions");
    size_t spid_count = 0, kid_count = 0;

    for (auto& path : spid_files) {
        spid_count += mora::emit_spid_facts(path.string(), db, cr.pool,
            cr.diags, next_rule_id, editor_id_map, loaded_plugins, ini_stats);
    }
    for (auto& path : kid_files) {
        kid_count += mora::emit_kid_facts(path.string(), db, cr.pool,
            cr.diags, next_rule_id, editor_id_map, loaded_plugins, ini_stats);
    }
    out.phase_done(fmt::format("{} SPID + {} KID \xe2\x86\x92 {} distribution facts",
        spid_count, kid_count, next_rule_id - 1));

    if (ini_stats.rules_dropped_missing_plugin > 0) {
        mora::log::info("    Dropped {} rules referencing {} missing plugins\n",
            ini_stats.rules_dropped_missing_plugin, ini_stats.missing_plugins.size());
        std::vector<std::pair<std::string, size_t>> sorted_missing(
            ini_stats.missing_plugins.begin(), ini_stats.missing_plugins.end());
        std::sort(sorted_missing.begin(), sorted_missing.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < std::min(sorted_missing.size(), size_t(5)); ++i) {
            mora::log::info("      {} ({} rules)\n",
                sorted_missing[i].first, sorted_missing[i].second);
        }
    }
    if (ini_stats.unresolved_editor_ids > 0) {
        mora::log::info("    {} editor IDs could not be resolved\n",
            ini_stats.unresolved_editor_ids);
    }

    return next_rule_id;
}

// Copy FactDB relations into columnar format and run pipeline evaluation
static void evaluate_columnar(
    CheckResult& cr, mora::FactDB& db, mora::PatchBuffer& patch_buf,
    mora::Output& out)
{
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
                vals[i] = value_to_u64(tuple[i]);
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

    copy_relation("spid_dist", {mora::ColType::U32, mora::ColType::U32, mora::ColType::U32});
    copy_relation("kid_dist", {mora::ColType::U32, mora::ColType::U32, mora::ColType::U32});

    col_store.build_all_indexes();

    out.phase_start("Evaluating (columnar)");
    mora::evaluate_distributions_columnar(col_store, cr.pool, patch_buf);
    patch_buf.sort_and_dedup();
    out.phase_done(fmt::format("{} patches generated", patch_buf.size()));
}

// Evaluate .mora rules via Datalog and merge results into PatchBuffer
static void evaluate_mora_rules(
    CheckResult& cr, mora::Evaluator& evaluator,
    mora::PatchBuffer& patch_buf, mora::Output& out)
{
    if (cr.modules.empty()) return;

    out.phase_start("Evaluating (.mora rules)");
    auto eval_progress = [&](size_t current, size_t total, std::string_view name) {
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

    // Convert PatchSet values into PatchBuffer entries
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
                    val = static_cast<uint64_t>(fp.value.as_int());
                    break;
                case mora::Value::Kind::Float: {
                    vtype = static_cast<uint8_t>(mora::PatchValueType::Float);
                    double d = fp.value.as_float();
                    std::memcpy(&val, &d, 8);
                    break;
                }
                default:
                    continue;
            }
            patch_buf.emit(rp.target_formid,
                           static_cast<uint8_t>(fp.field),
                           static_cast<uint8_t>(fp.op),
                           vtype, val);
        }
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
        if (r.cardinality != mora::model::Cardinality::Set) continue;

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
    mora::PatchBuffer& patch_buf, const std::string& target_path,
    const std::string& output_dir, mora::Output& out,
    const mora::PluginSet& loaded_plugins,
    const mora::FactDB& facts, mora::StringPool& pool,
    const std::vector<mora::Module>& modules)
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

    // Compile dynamic rules to an operator DAG and emit as DagBytecode section.
    mora::dag::DagGraph dag_graph;
    for (const auto& m : modules) {
        mora::dag::compile_dynamic_rules(m, pool, dag_graph);
    }
    std::vector<uint8_t> dag_payload;
    if (dag_graph.node_count() > 0) {
        dag_payload = mora::dag::serialize_dag(dag_graph);
    }

    out.phase_start("Serializing patches");
    auto patch_data = mora::serialize_patch_table(
        patch_buf.entries(), digest, arrangements_section, dag_payload);
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

// Scan for SkyPatcher INI directories and parse into a Module
static mora::Module load_skypatcher_rules(
    const std::string& target_path, mora::StringPool& pool,
    mora::DiagBag& diags, mora::Output& out,
    const mora::FormIdResolver* resolver,
    const mora::EditorIdMap* editor_ids = nullptr)
{
    mora::Module mod;
    mod.filename = "<imported SkyPatcher rules>";

    // Same directory scanning logic as cmd_import
    fs::path skypatcher_dir;
    for (auto& candidate : {
        fs::path(target_path) / "SKSE" / "Plugins" / "SkyPatcher",
        fs::path(target_path) / "SkyPatcher",
        fs::path(target_path),
    }) {
        if (!fs::exists(candidate) || !fs::is_directory(candidate)) continue;
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

    if (skypatcher_dir.empty()) return mod;

    out.phase_start("Loading SkyPatcher configs");
    mora::SkyPatcherParser parser(pool, diags, resolver, editor_ids);
    auto rules = parser.parse_directory(skypatcher_dir.string());
    for (auto& r : rules) mod.rules.push_back(std::move(r));
    out.phase_done(fmt::format("{} SkyPatcher rules from {}", mod.rules.size(), skypatcher_dir.string()));

    return mod;
}

static int cmd_compile(const std::string& target_path, const std::string& output_dir,
                        const std::string& data_dir,
                        mora::Output& out, bool use_color) {
    auto start = std::chrono::steady_clock::now();
    out.print_header("0.1.0");

    CheckResult cr;
    if (!run_check_pipeline(cr, target_path, out, use_color, /*import_ini=*/false)) return 1;

    if (cr.diags.has_errors()) {
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        out.failure(fmt::format("Failed with {} error(s) in {}ms \xe2\x80\x94 aborting compilation",
            cr.diags.error_count(), total_ms));
        return 1;
    }

    // Phase classification
    out.phase_start("Classifying");
    mora::PhaseClassifier classifier(cr.pool);
    size_t static_count = 0, dynamic_count = 0;

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

    if (!data_dir.empty()) {
        load_esp_data(cr, db, evaluator, data_dir, out, editor_id_map, loaded_plugins);
    }

    // Emit plugin_loaded facts for SkyPatcher hasPlugins/hasPluginsOr filters
    {
        auto rel_id = cr.pool.intern(mora::rel::kPluginLoaded);
        db.configure_relation(rel_id, 1, {0});
        for (auto& plugin_name : loaded_plugins) {
            db.add_fact(rel_id, {mora::Value::make_string(cr.pool.intern(plugin_name))});
        }
    }

    // Emit form_source(FormID, "Plugin.esp") facts by inspecting load order index
    // The high byte of each FormID encodes the load order index.
    if (!data_dir.empty()) {
        mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);
        auto source_rel = cr.pool.intern(mora::rel::kFormSource);
        db.configure_relation(source_rel, 2, {0});
        // Scan all existence relations for FormIDs and derive their source plugin
        const char* existence_rels[] = {
            mora::rel::kNpc, mora::rel::kWeapon, mora::rel::kArmor, mora::rel::kAmmo,
            mora::rel::kPotion, mora::rel::kBook, mora::rel::kSpell, mora::rel::kMiscItem,
            mora::rel::kMagicEffect, mora::rel::kIngredient, mora::rel::kScroll,
            mora::rel::kSoulGem, mora::rel::kEnchantment, mora::rel::kLeveledList,
        };
        for (auto* rel_name : existence_rels) {
            auto rid = cr.pool.intern(rel_name);
            for (auto& tuple : db.get_relation(rid)) {
                if (tuple.empty() || tuple[0].kind() != mora::Value::Kind::FormID) continue;
                uint32_t formid = tuple[0].as_formid();
                uint8_t load_idx = static_cast<uint8_t>(formid >> 24);
                if (load_idx < lo.plugins.size()) {
                    auto plugin_name = lo.plugins[load_idx].filename().string();
                    db.add_fact(source_rel, {tuple[0],
                        mora::Value::make_string(cr.pool.intern(plugin_name))});
                }
            }
        }
    }

    // Derive npc_gender(FormID, "male"/"female") from npc_flags ACBS data
    {
        auto flags_rel = cr.pool.intern(mora::rel::kNpcFlags);
        auto gender_rel = cr.pool.intern(mora::rel::kNpcGender);
        db.configure_relation(gender_rel, 2, {0});
        auto male_sid = cr.pool.intern(mora::gender::kMale);
        auto female_sid = cr.pool.intern(mora::gender::kFemale);
        for (auto& tuple : db.get_relation(flags_rel)) {
            if (tuple.size() < 2) continue;
            uint32_t flags = static_cast<uint32_t>(tuple[1].as_int());
            bool is_female = (flags & mora::npc_flags::kFemale) != 0;
            db.add_fact(gender_rel, {tuple[0],
                mora::Value::make_string(is_female ? female_sid : male_sid)});
        }
    }

    uint32_t next_rule_id = load_ini_distributions(
        target_path, cr, db, out, editor_id_map, loaded_plugins);

    // Load SkyPatcher INI configs as Datalog rules
    {
        mora::FormIdResolver resolver;
        if (!editor_id_map.empty()) {
            resolver.build_from_editor_ids(editor_id_map);
        }
        const mora::FormIdResolver* resolver_ptr = resolver.has_data() ? &resolver : nullptr;
        const mora::EditorIdMap* edid_ptr = editor_id_map.empty() ? nullptr : &editor_id_map;
        auto sky_mod = load_skypatcher_rules(target_path, cr.pool, cr.diags, out, resolver_ptr, edid_ptr);
        if (!sky_mod.rules.empty()) {
            cr.rule_count += sky_mod.rules.size();
            cr.modules.push_back(std::move(sky_mod));
        }
    }

    mora::PatchBuffer patch_buf;

    if (next_rule_id > 1) {
        evaluate_columnar(cr, db, patch_buf, out);
    }

    evaluate_mora_rules(cr, evaluator, patch_buf, out);

    out.phase_done(fmt::format("{} total patches", patch_buf.size()));

    // Write patch file
    int write_rc = write_patch_file(patch_buf, target_path, output_dir, out, loaded_plugins, db, cr.pool, cr.modules);
    if (write_rc != 0) return write_rc;

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
        mora::DiagRenderer renderer(use_color);
        mora::log::info("{}", renderer.render_all(diags));
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
                mora::log::info("    {}: {} {}\n", field_name(fp.field), op_prefix(fp.op), format_value(fp.value));
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

    fs::path cache_dir = base / "MoraCache";
    fs::path dll_path = cache_dir / "MoraRuntime.dll";

    if (!fs::exists(cache_dir) || !fs::exists(dll_path)) {
        mora::log::info("  Cache status:  no DLL (run mora compile)\n");
    } else {
        auto dll_size = fs::file_size(dll_path);
        mora::log::info("  Cache status:  MoraRuntime.dll ({})\n", format_bytes(dll_size));
    }

    if (!data_dir.empty()) {
        mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);
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

// Colorize :Symbol tokens in a rule line for display
static std::string colorize_symbols(const std::string& line, bool use_color) {
    if (!use_color) return line;
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
    return colored;
}

// Print a rule with colored formatting
static void print_rule_colored(const mora::Rule& rule, const std::string& fallback_file,
                                mora::MoraPrinter& printer, mora::Output& out, bool use_color) {
    // Source comment
    std::string src_file = rule.span.file.empty() ? fallback_file : rule.span.file;
    if (auto pos = src_file.rfind('/'); pos != std::string::npos)
        src_file = src_file.substr(pos + 1);
    auto comment = fmt::format("Imported from {}:{}", src_file, rule.span.start_line);
    mora::log::info("  {}\n", mora::TermStyle::dim(printer.print_comment(comment), use_color));

    std::string rule_text = printer.print_rule(rule);
    std::istringstream ss(rule_text);
    std::string line;
    bool first_line = true;
    while (std::getline(ss, line)) {
        if (first_line) {
            mora::log::info("  {}\n", mora::TermStyle::bold(line, use_color));
            first_line = false;
        } else if (!line.empty()) {
            mora::log::info("  {}\n", colorize_symbols(line, use_color));
        } else {
            mora::log::info("\n");
        }
    }
    mora::log::info("\n");
}

static int cmd_import(const std::string& target_path, const std::string& data_dir,
                      bool use_color) {
    mora::Output out(use_color, mora::stdout_is_tty());
    out.print_header("0.1.0");

    auto spid_files = find_files_by_suffix(target_path, "_DISTR.ini");
    auto kid_files  = find_files_by_suffix(target_path, "_KID.ini");

    // Look for SkyPatcher directory
    fs::path skypatcher_dir;
    for (auto& candidate : {
        fs::path(target_path) / "SKSE" / "Plugins" / "SkyPatcher",
        fs::path(target_path) / "SkyPatcher",
        fs::path(target_path),
    }) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
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
        mora::log::info("  No INI files found in {}\n", target_path);
        return 1;
    }

    mora::StringPool pool;
    mora::DiagBag diags;

    // Load ESP data for FormID resolution (optional but improves output)
    mora::FormIdResolver resolver;
    if (!data_dir.empty()) {
        out.phase_start("Loading ESP data for symbol resolution");
        mora::SchemaRegistry schema(pool);
        schema.register_defaults();
        mora::FactDB db(pool);
        schema.configure_fact_db(db);
        mora::LoadOrder lo = mora::LoadOrder::from_directory(data_dir);
        mora::EspReader esp_reader(pool, diags, schema);
        esp_reader.read_load_order(lo.plugins, db);
        resolver.build_from_editor_ids(esp_reader.editor_id_map());
        out.phase_done(fmt::format("{} EditorIDs loaded",
            resolver.has_data() ? esp_reader.editor_id_map().size() : 0));
        mora::log::info("\n");
    }

    mora::log::info("  Scanning for INI files...\n\n");

    mora::MoraPrinter printer(pool);
    const mora::FormIdResolver* resolver_ptr = resolver.has_data() ? &resolver : nullptr;

    size_t total_spid_rules = 0;
    size_t total_kid_rules  = 0;

    for (auto& path : spid_files) {
        mora::SpidParser parser(pool, diags, resolver_ptr);
        auto rules = parser.parse_file(path.string());
        std::string fname = path.filename().string();
        out.section_header(fmt::format("{} ({} rules)", fname, rules.size()));
        mora::log::info("\n");
        for (auto& rule : rules) {
            print_rule_colored(rule, fname, printer, out, use_color);
        }
        total_spid_rules += rules.size();
    }

    for (auto& path : kid_files) {
        mora::KidParser parser(pool, diags, resolver_ptr);
        auto rules = parser.parse_file(path.string());
        std::string fname = path.filename().string();
        out.section_header(fmt::format("{} ({} rules)", fname, rules.size()));
        mora::log::info("\n");
        for (auto& rule : rules) {
            print_rule_colored(rule, fname, printer, out, use_color);
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
            out.section_header(fmt::format("SkyPatcher ({}) ({} rules)",
                skypatcher_dir.string(), sky_rules.size()));
            mora::log::info("\n");

            for (auto& rule : sky_rules) {
                std::string rule_text = printer.print_rule(rule);
                std::istringstream ss(rule_text);
                std::string line;
                bool first_line = true;
                while (std::getline(ss, line)) {
                    if (first_line) {
                        mora::log::info("  {}\n", mora::TermStyle::bold(line, use_color));
                        first_line = false;
                    } else if (!line.empty()) {
                        mora::log::info("  {}\n", line);
                    } else {
                        mora::log::info("\n");
                    }
                }
                mora::log::info("\n");
            }

            total_skypatcher_rules = sky_rules.size();
        }

        for (auto& entry : fs::recursive_directory_iterator(skypatcher_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ini")
                skypatcher_file_count++;
        }
    }

    // Summary
    size_t total_rules = total_spid_rules + total_kid_rules + total_skypatcher_rules;
    size_t total_files = spid_files.size() + kid_files.size() + skypatcher_file_count;

    out.section_header("Summary");

    auto gs = [use_color](auto v) { return mora::TermStyle::green(fmt::format("{}", v), use_color); };
    auto pl = [](size_t n) -> const char* { return n == 1 ? "" : "s"; };

    mora::log::info("  {} INI files \xe2\x86\x92 {} Mora rules\n", gs(total_files), gs(total_rules));

    if (!spid_files.empty()) {
        mora::log::info("    SPID: {} file{}, {} rule{}\n",
            gs(spid_files.size()), pl(spid_files.size()),
            gs(total_spid_rules), pl(total_spid_rules));
    }
    if (!kid_files.empty()) {
        mora::log::info("    KID:  {} file{}, {} rule{}\n",
            gs(kid_files.size()), pl(kid_files.size()),
            gs(total_kid_rules), pl(total_kid_rules));
    }
    if (total_skypatcher_rules > 0) {
        mora::log::info("    SkyPatcher: {} file{}, {} rule{}\n",
            gs(skypatcher_file_count), pl(skypatcher_file_count),
            gs(total_skypatcher_rules), pl(total_skypatcher_rules));
    }
    mora::log::info("\n");

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

    if (data_dir.empty() && (command == "compile" || command == "info" || command == "import")) {
        data_dir = detect_skyrim_data_dir();
    }

    bool use_color = !force_no_color && mora::color_enabled();
    bool is_tty = mora::stdout_is_tty();
    mora::Output out(use_color, is_tty);

    if (command == "check") {
        return cmd_check(target_path, out, use_color);
    }

    if (command == "compile") {
        return cmd_compile(target_path, output_dir, data_dir, out, use_color);
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

    if (command == "docs") {
        mora::generate_docs(std::cout);
        return 0;
    }

    print_usage();
    return 1;
}
