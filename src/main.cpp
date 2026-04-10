#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include "mora/diag/diagnostic.h"
#include "mora/diag/renderer.h"
#include "mora/cli/terminal.h"
#include "mora/cli/progress.h"
#include "mora/core/string_pool.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

static void print_usage() {
    std::printf("Usage: mora <command> [options] [path]\n\n");
    std::printf("Commands:\n");
    std::printf("  check     Type check and lint .mora files\n");
    std::printf("  compile   Compile .mora files (not yet implemented)\n");
    std::printf("  inspect   Dump .spatch contents (not yet implemented)\n");
    std::printf("  info      Show project status (not yet implemented)\n");
    std::printf("\nOptions:\n");
    std::printf("  --no-color    Disable colored output\n");
    std::printf("  -v            Verbose output\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    std::string command = argv[1];
    bool force_no_color = false;
    [[maybe_unused]] bool verbose = false;
    std::string target_path = ".";

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-color") force_no_color = true;
        else if (arg == "-v") verbose = true;
        else target_path = arg;
    }

    bool use_color = !force_no_color && mora::color_enabled();
    bool is_tty = mora::stdout_is_tty();
    mora::Progress progress(use_color, is_tty);

    if (command == "check") {
        auto start = std::chrono::steady_clock::now();
        progress.print_header("0.1.0");

        auto files = find_mora_files(target_path);
        if (files.empty()) {
            std::fprintf(stderr, "  No .mora files found in %s\n", target_path.c_str());
            return 1;
        }

        mora::StringPool pool;
        mora::DiagBag diags;

        // Parse
        progress.start_phase("Parsing");
        std::vector<mora::Module> modules;
        // Keep source strings alive for the duration of parsing (Lexer stores string_views into them)
        std::vector<std::string> sources;
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
        auto parse_end = std::chrono::steady_clock::now();
        auto parse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(parse_end - start).count();
        progress.finish_phase(
            "Parsing " + std::to_string(files.size()) + " files",
            std::to_string(parse_ms) + "ms");

        // Resolve
        progress.start_phase("Resolving");
        mora::NameResolver resolver(pool, diags);
        size_t rule_count = 0;
        for (auto& mod : modules) {
            resolver.resolve(mod);
            rule_count += mod.rules.size();
        }
        auto resolve_end = std::chrono::steady_clock::now();
        auto resolve_ms = std::chrono::duration_cast<std::chrono::milliseconds>(resolve_end - parse_end).count();
        progress.finish_phase(
            "Resolving " + std::to_string(rule_count) + " rules",
            std::to_string(resolve_ms) + "ms");

        // Type check
        progress.start_phase("Type checking");
        mora::TypeChecker checker(pool, diags, resolver);
        for (auto& mod : modules) {
            checker.check(mod);
        }
        auto check_end = std::chrono::steady_clock::now();
        auto check_ms = std::chrono::duration_cast<std::chrono::milliseconds>(check_end - resolve_end).count();
        progress.finish_phase(
            "Type checking " + std::to_string(rule_count) + " rules",
            std::to_string(check_ms) + "ms");

        // Render diagnostics
        mora::DiagRenderer renderer(use_color);
        if (!diags.all().empty()) {
            std::printf("\n");
            std::printf("%s", renderer.render_all(diags).c_str());
        }

        auto total_end = std::chrono::steady_clock::now();
        auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - start).count();

        if (diags.has_errors()) {
            progress.print_failure("Failed with " + std::to_string(diags.error_count()) +
                " error(s) in " + std::to_string(total_ms) + "ms");
            return 1;
        } else {
            std::string msg = "Checked " + std::to_string(rule_count) + " rules in " +
                std::to_string(total_ms) + "ms";
            if (diags.warning_count() > 0) {
                msg += " (" + std::to_string(diags.warning_count()) + " warnings)";
            }
            progress.print_success(msg);
            return 0;
        }
    }

    if (command == "compile" || command == "inspect" || command == "info" || command == "dump") {
        std::fprintf(stderr, "  '%s' command not yet implemented (see Plan 2)\n", command.c_str());
        return 1;
    }

    print_usage();
    return 1;
}
