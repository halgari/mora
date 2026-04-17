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

// Locate a Plugins.txt / plugins.txt alongside or near `data_dir`.
// Preference order: Data/../Plugins.txt (co-located, matches our
// self-hosted CI image layout) → conventional Proton AppData prefixes
// → Windows %LOCALAPPDATA%. Returns "" when no file is found; caller
// falls back to directory-walk ordering in that case.
static std::string detect_plugins_txt(const std::string& data_dir) {
    std::vector<fs::path> candidates;

    if (!data_dir.empty()) {
        fs::path base(data_dir);
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
        fs::path h(home);
        // Steam + Proton prefix for Skyrim SE (app id 489830).
        candidates.push_back(h / ".local/share/Steam/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition/Plugins.txt");
        // GOG portable prefix layout used by scripts/deploy_runtime.sh.
        candidates.push_back(h / "Games/gog/the-elder-scrolls-v-skyrim-special-edition/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition GOG/Plugins.txt");
    }
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
        "  lsp       Run the language server over stdio (used by editors)\n"
        "\nOptions:\n"
        "  --no-color        Disable colored output\n"
        "  --output DIR      Output directory (compile, default: MoraCache/)\n"
        "  --data-dir DIR    Skyrim Data/ directory for ESP loading (compile, info)\n"
        "  --plugins-txt F   Path to plugins.txt (load-order source, compile)\n"
        "  --conflicts       Show only conflict info (inspect)\n"
        "  -v                Verbose output\n");
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

// Load ESP plugin data into the FactDB via parallel batched reading
static void load_esp_data(
    CheckResult& cr, mora::FactDB& db, mora::Evaluator& evaluator,
    const std::string& data_dir, const std::string& plugins_txt,
    mora::Output& out,
    mora::EditorIdMap& editor_id_map, mora::PluginSet& loaded_plugins)
{
    out.phase_start("Loading ESPs");

    mora::SchemaRegistry schema(cr.pool);
    schema.register_defaults();
    schema.configure_fact_db(db);

    // A plugins.txt (either CLI-provided or auto-detected) is the
    // authoritative source for both which plugins to load and their
    // load-order indices. Without one the compiler falls back to a
    // directory walk that doesn't match runtime ordering — see issue
    // #5 for why that's broken for anything beyond Skyrim.esm.
    mora::LoadOrder lo = !plugins_txt.empty()
        ? mora::LoadOrder::from_plugins_txt(plugins_txt, data_dir)
        : mora::LoadOrder::from_directory(data_dir);
    for (auto& p : lo.plugins) {
        loaded_plugins.insert(p.filename().string());
    }
    if (!plugins_txt.empty()) {
        mora::log::info("  Load order:    {} ({} plugins)\n", plugins_txt, lo.plugins.size());
    }

    // Build the runtime-index map once, share it across all reader
    // threads. Holds no per-reader state so this is safe to alias.
    auto runtime_index = lo.runtime_index_map();

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
                reader.set_runtime_index_map(&runtime_index);
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

static int cmd_compile(const std::string& target_path, const std::string& output_dir,
                        const std::string& data_dir, const std::string& plugins_txt,
                        mora::Output& out, bool use_color) {
    auto start = std::chrono::steady_clock::now();
    out.print_header("0.1.0");

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
        load_esp_data(cr, db, evaluator, data_dir, plugins_txt, out, editor_id_map, loaded_plugins);
    }

    mora::PatchBuffer patch_buf;
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


// ── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    std::string command = argv[1];
    bool force_no_color = false;
    bool show_conflicts = false;
    std::string target_path = ".";
    std::string output_dir = "MoraCache";
    std::string data_dir;
    std::string plugins_txt;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-color") force_no_color = true;
        else if (arg == "--conflicts") show_conflicts = true;
        else if (arg == "--output" && i + 1 < argc) { output_dir = argv[++i]; }
        else if (arg == "--data-dir" && i + 1 < argc) { data_dir = argv[++i]; }
        else if (arg == "--plugins-txt" && i + 1 < argc) { plugins_txt = argv[++i]; }
        else target_path = arg;
    }

    if (data_dir.empty() && (command == "compile" || command == "info")) {
        data_dir = detect_skyrim_data_dir();
    }

    if (plugins_txt.empty() && command == "compile" && !data_dir.empty()) {
        plugins_txt = detect_plugins_txt(data_dir);
    }

    bool use_color = !force_no_color && mora::color_enabled();
    bool is_tty = mora::stdout_is_tty();
    mora::Output out(use_color, is_tty);

    if (command == "check") {
        return cmd_check(target_path, out, use_color);
    }

    if (command == "compile") {
        return cmd_compile(target_path, output_dir, data_dir, plugins_txt, out, use_color);
    }

    if (command == "inspect") {
        return cmd_inspect(target_path, show_conflicts, use_color);
    }

    if (command == "info") {
        return cmd_info(target_path, data_dir);
    }

    if (command == "docs") {
        mora::generate_docs(std::cout);
        return 0;
    }

    if (command == "lsp") return mora::lsp::run(argc - 2, argv + 2);

    print_usage();
    return 1;
}
