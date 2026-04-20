#include "mora_skyrim_compile/kid_compiler.h"

#include "mora_skyrim_compile/kid_parser.h"
#include "mora_skyrim_compile/kid_resolver.h"
#include "mora_skyrim_compile/kid_util.h"

#include "mora/cli/log.h"

#include <filesystem>
#include <fmt/format.h>

namespace mora_skyrim_compile {

KidCompileResult compile_kid_modules(
    const KidCompileInputs& inputs,
    mora::StringPool&       pool,
    mora::DiagBag&          diags)
{
    KidCompileResult out;

    // Discover *_KID.ini files non-recursively. Prefer kid_dir when set,
    // otherwise use data_dir (matches KID's own runtime: it reads only
    // files directly in Data/).
    namespace fs = std::filesystem;
    const fs::path& scan_dir = !inputs.kid_dir.empty() ? inputs.kid_dir : inputs.data_dir;
    if (scan_dir.empty() || !fs::exists(scan_dir)) return out;

    std::vector<fs::path> files;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(scan_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        // Match `*_KID.ini` case-insensitively — KID's own runtime
        // accepts any casing of the suffix. std::filesystem preserves
        // the on-disk spelling, so we lower the tail ourselves.
        std::string const filename = entry.path().filename().string();
        static constexpr std::string_view kSuffix = "_kid.ini";
        if (filename.size() < kSuffix.size()) continue;
        if (to_lower(std::string_view(filename).substr(
                filename.size() - kSuffix.size())) != kSuffix) continue;
        files.push_back(entry.path());
    }
    if (files.empty()) return out;

    // The resolver needs the EditorID map populated by the ESP source.
    // If the caller hasn't passed one we can't resolve references —
    // emit one info-level warning and bail.
    if (!inputs.editor_ids) {
        diags.warning(
            "kid-no-editor-ids",
            fmt::format("{} *_KID.ini file(s) skipped: no EditorID map "
                        "supplied to compile_kid_modules", files.size()),
            mora::SourceSpan{}, "");
        return out;
    }

    std::sort(files.begin(), files.end());  // deterministic rule ordering
    mora::log::info("  KID INI:       {} file(s) under {}\n",
                    files.size(), scan_dir.string());

    for (const auto& path : files) {
        auto parsed = parse_kid_file(path);
        out.lines_parsed += parsed.lines.size();
        auto rfile = resolve_kid_file(parsed, *inputs.editor_ids, pool, diags);
        for (auto& r : rfile.rules) out.module.rules.push_back(std::move(r));
    }

    out.module.filename = "<synthesized:kid>";
    mora::log::info("                 {} lines -> {} rules\n",
                    out.lines_parsed, out.module.rules.size());
    return out;
}

} // namespace mora_skyrim_compile
