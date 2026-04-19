#include "mora_skyrim_compile/kid_data_source.h"

#include "mora_skyrim_compile/kid_parser.h"
#include "mora_skyrim_compile/kid_resolver.h"

#include "mora/cli/log.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <string>

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

KidDataSource::KidDataSource()
    : provides_{
          "ini/kid_dist",
          "ini/kid_filter",
          "ini/kid_exclude",
          "ini/kid_trait",
      } {}

KidDataSource::~KidDataSource() = default;

std::string_view KidDataSource::name() const {
    return "skyrim.kid";
}

std::span<const std::string> KidDataSource::provides() const {
    return provides_;
}

void KidDataSource::load(mora::ext::LoadCtx& ctx, mora::FactDB& out) {
    // --no-kid opt-out honored here even though load_required has
    // already decided to invoke us: registration can't be undone at
    // dispatch time, and this keeps the opt-out local to the source
    // that owns it.
    if (!ctx.kid_enabled) return;

    // Discover *_KID.ini files non-recursively. Prefer the CLI-supplied
    // --kid-dir override; otherwise fall back to --data-dir (KID
    // itself reads only files directly in Data/).
    namespace fs = std::filesystem;
    const fs::path& scan_dir = !ctx.kid_dir.empty() ? ctx.kid_dir : ctx.data_dir;
    if (scan_dir.empty() || !fs::exists(scan_dir)) return;

    std::vector<fs::path> files;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(scan_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (ends_with_kid_ini(entry.path().filename().string())) {
            files.push_back(entry.path());
        }
    }
    if (files.empty()) return;

    // The resolver needs the EditorID map accumulated by the ESP source.
    // If the caller didn't opt into accumulation (editor_ids_out is
    // null) we can't resolve references — emit one info-level warning
    // and bail. When the map is empty (no ESPs loaded) resolution will
    // warn per reference; that's handled by the resolver.
    if (!ctx.editor_ids_out) {
        ctx.diags.warning(
            "kid-no-editor-ids",
            fmt::format("{} *_KID.ini file(s) skipped: SkyrimEspDataSource did "
                        "not populate ctx.editor_ids_out",
                        files.size()),
            mora::SourceSpan{}, "");
        return;
    }

    std::sort(files.begin(), files.end());  // deterministic fact ordering
    mora::log::info("  KID INI:       {} file(s) under {}\n",
                    files.size(), ctx.data_dir.string());

    uint32_t next_rule_id = 1;
    auto rel_cache = [&](const std::string& n) {
        return ctx.pool.intern(n);
    };

    size_t total_lines  = 0;
    size_t total_facts  = 0;
    for (const auto& path : files) {
        auto parsed = parse_kid_file(path);
        total_lines += parsed.lines.size();
        auto emissions = resolve_kid_file(parsed, *ctx.editor_ids_out,
                                           ctx.plugin_runtime_index_out,
                                           ctx.pool, ctx.diags, next_rule_id);
        for (auto& e : emissions) {
            out.add_fact(rel_cache(e.relation), std::move(e.values));
            ++total_facts;
        }
    }
    mora::log::info("                 {} lines -> {} facts ({} rules)\n",
                    total_lines, total_facts, next_rule_id - 1);
}

} // namespace mora_skyrim_compile
