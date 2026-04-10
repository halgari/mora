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
#include "mora/emit/patch_writer.h"
#include "mora/emit/patch_reader.h"
#include "mora/emit/rt_writer.h"
#include "mora/emit/lock_file.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

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

static void print_usage() {
    std::printf("Usage: mora <command> [options] [path]\n\n");
    std::printf("Commands:\n");
    std::printf("  check     Type check and lint .mora files\n");
    std::printf("  compile   Compile .mora files to patch/runtime output\n");
    std::printf("  inspect   Display .mora.patch file contents\n");
    std::printf("  info      Show project status overview\n");
    std::printf("\nOptions:\n");
    std::printf("  --no-color    Disable colored output\n");
    std::printf("  --output DIR  Output directory (compile, default: MoraCache/)\n");
    std::printf("  --conflicts   Show only conflict info (inspect)\n");
    std::printf("  -v            Verbose output\n");
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
    mora::Progress& progress,
    [[maybe_unused]] bool use_color)
{
    result.files = find_mora_files(target_path);
    if (result.files.empty()) {
        std::fprintf(stderr, "  No .mora files found in %s\n", target_path.c_str());
        return false;
    }

    // Parse
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
    progress.finish_phase(
        "Parsing " + std::to_string(result.files.size()) + " files",
        "done");

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
                        mora::Progress& progress, bool use_color) {
    auto start = std::chrono::steady_clock::now();
    progress.print_header("0.1.0");

    CheckResult cr;
    if (!run_check_pipeline(cr, target_path, progress, use_color)) return 1;

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
    mora::PatchSet all_patches;

    for (auto& mod : cr.modules) {
        auto mod_patches = evaluator.evaluate_static(mod);
        // Merge by resolving and re-adding
        auto resolved = mod_patches.resolve();
        for (auto& rp : resolved.all_patches_sorted()) {
            for (auto& fp : rp.fields) {
                all_patches.add_patch(rp.target_formid, fp.field, fp.op, fp.value, fp.source_mod, fp.priority);
            }
        }
    }

    auto final_resolved = all_patches.resolve();
    progress.finish_phase(
        std::to_string(final_resolved.patch_count()) + " patches generated",
        "done");

    // Emit
    progress.start_phase("Emitting");

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

    // Compute hashes
    uint64_t source_hash = mora::LockFile::hash_files(cr.files);
    uint64_t load_order_hash = mora::LockFile::hash_string("default_load_order");

    // Write .mora.patch
    fs::path patch_path = out_path / "mora.patch";
    {
        std::ofstream out(patch_path, std::ios::binary);
        mora::PatchWriter writer(cr.pool);
        writer.write(out, final_resolved, load_order_hash, source_hash);
    }

    // Write .mora.rt
    fs::path rt_path = out_path / "mora.rt";
    {
        std::ofstream out(rt_path, std::ios::binary);
        mora::RtWriter writer(cr.pool);
        writer.write(out, dynamic_rules);
    }

    // Write .mora.lock
    fs::path lock_path = out_path / "mora.lock";
    mora::LockFile lock;
    lock.source_hash = source_hash;
    lock.load_order_hash = load_order_hash;
    lock.write(lock_path);

    progress.finish_phase(
        "Output to " + out_path.string(),
        "done");

    // Summary
    auto patch_size = fs::file_size(patch_path);
    progress.print_summary(
        static_count,
        final_resolved.patch_count(),
        dynamic_count,
        final_resolved.get_conflicts().size(),
        cr.diags.error_count(),
        cr.diags.warning_count(),
        format_bytes(patch_size));

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

static int cmd_inspect(const std::string& target_path, bool show_conflicts) {
    fs::path patch_path = target_path;
    if (fs::is_directory(patch_path)) {
        patch_path = patch_path / "MoraCache" / "mora.patch";
    }
    // If target is "." or a directory without the file, also try default
    if (!fs::exists(patch_path) && !patch_path.has_extension()) {
        patch_path = fs::path(target_path) / "MoraCache" / "mora.patch";
    }

    if (!fs::exists(patch_path)) {
        std::fprintf(stderr, "error: patch file not found: %s\n", patch_path.string().c_str());
        return 1;
    }

    mora::StringPool pool;
    mora::PatchReader reader(pool);

    std::ifstream in(patch_path, std::ios::binary);
    auto result = reader.read(in);
    if (!result) {
        std::fprintf(stderr, "error: failed to read patch file: %s\n", patch_path.string().c_str());
        return 1;
    }

    auto file_size = fs::file_size(patch_path);

    if (show_conflicts) {
        std::printf("  no conflict data in patch file\n");
        return 0;
    }

    // Header
    std::printf("  mora.patch v%u — %zu patches (%s)\n",
        result->version,
        result->patches.size(),
        format_bytes(file_size).c_str());
    std::printf("  load order hash: %016llX\n", (unsigned long long)result->load_order_hash);
    std::printf("  source hash:     %016llX\n", (unsigned long long)result->source_hash);
    std::printf("\n");

    if (result->patches.empty()) {
        std::printf("  (no patches)\n");
    } else {
        for (auto& rp : result->patches) {
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

static int cmd_info(const std::string& target_path) {
    std::printf("  mora v0.1.0\n\n");

    // Find and count .mora files/rules
    fs::path base(target_path);
    if (fs::is_regular_file(base)) {
        base = base.parent_path();
    }

    auto files = find_mora_files(base);
    size_t rule_count = 0;

    if (!files.empty()) {
        mora::StringPool pool;
        mora::DiagBag diags;
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

    // Check cache
    fs::path cache_dir = base / "MoraCache";
    fs::path lock_path = cache_dir / "mora.lock";

    if (!fs::exists(cache_dir) || !fs::exists(lock_path)) {
        std::printf("  Cache status:  no cache\n");
    } else {
        auto lock = mora::LockFile::read(lock_path);
        uint64_t current_hash = mora::LockFile::hash_files(files);
        if (lock.matches(current_hash, lock.load_order_hash)) {
            std::printf("  Cache status:  up to date\n");
        } else {
            std::printf("  Cache status:  stale\n");
        }
    }

    return 0;
}

// ── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    std::string command = argv[1];
    bool force_no_color = false;
    [[maybe_unused]] bool verbose = false;
    bool show_conflicts = false;
    std::string target_path = ".";
    std::string output_dir = "MoraCache";

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-color") force_no_color = true;
        else if (arg == "-v") verbose = true;
        else if (arg == "--conflicts") show_conflicts = true;
        else if (arg == "--output" && i + 1 < argc) { output_dir = argv[++i]; }
        else target_path = arg;
    }

    bool use_color = !force_no_color && mora::color_enabled();
    bool is_tty = mora::stdout_is_tty();
    mora::Progress progress(use_color, is_tty);

    if (command == "check") {
        return cmd_check(target_path, progress, use_color);
    }

    if (command == "compile") {
        return cmd_compile(target_path, output_dir, progress, use_color);
    }

    if (command == "inspect") {
        return cmd_inspect(target_path, show_conflicts);
    }

    if (command == "info") {
        return cmd_info(target_path);
    }

    print_usage();
    return 1;
}
