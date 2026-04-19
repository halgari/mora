#include "mora_skyrim_compile/kid_compiler.h"

#include "mora_skyrim_compile/kid_parser.h"
#include "mora_skyrim_compile/kid_resolver.h"

#include "mora/cli/log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>

namespace mora_skyrim_compile {

namespace {

bool ends_with_kid_ini(const std::string& filename) {
    static constexpr std::string_view kSuffix = "_kid.ini";
    if (filename.size() < kSuffix.size()) return false;
    std::string tail = filename.substr(filename.size() - kSuffix.size());
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return tail == kSuffix;
}

} // namespace

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
        if (ends_with_kid_ini(entry.path().filename().string())) {
            files.push_back(entry.path());
        }
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

    // Synthetic-editor-id dedup across all files.
    std::unordered_map<std::string, uint32_t> synth_seen;

    for (const auto& path : files) {
        auto parsed = parse_kid_file(path);
        out.lines_parsed += parsed.lines.size();
        auto rfile = resolve_kid_file(parsed, *inputs.editor_ids,
                                       inputs.plugin_runtime_index,
                                       pool, diags);
        for (auto& r : rfile.rules) out.module.rules.push_back(std::move(r));
        for (auto& [edid, fid] : rfile.synthetic_editor_ids) {
            synth_seen.try_emplace(std::move(edid), fid);
        }
    }

    out.synthetic_editor_ids.reserve(synth_seen.size());
    for (auto& kv : synth_seen) out.synthetic_editor_ids.emplace_back(kv.first, kv.second);

    out.module.filename = "<synthesized:kid>";
    mora::log::info("                 {} lines -> {} rules\n",
                    out.lines_parsed, out.module.rules.size());
    return out;
}

} // namespace mora_skyrim_compile
