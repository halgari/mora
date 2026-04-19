#include "mora_skyrim_compile/kid_resolver.h"

#include "mora/core/source_location.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/runtime_index.h"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>

namespace mora_skyrim_compile {

namespace {

std::string to_lower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

enum class ResolveError {
    None,
    MissingEditorId,
    FormidUnsupported,  // caller didn't wire plugin_runtime_index
    MissingPlugin,      // plugin named in `0xNNN~Plugin.ext` isn't loaded
};

ResolveError resolve_ref(
    const KidRef&                                     ref,
    const std::unordered_map<std::string, uint32_t>&  editor_ids,
    const std::unordered_map<std::string, uint32_t>*  plugin_runtime_index,
    uint32_t&                                         out_formid) {
    if (ref.is_editor_id()) {
        // Exact match first — ESP extraction preserves casing as-written.
        auto it = editor_ids.find(ref.editor_id);
        if (it != editor_ids.end()) {
            out_formid = it->second;
            return ResolveError::None;
        }
        // Case-insensitive fallback. KID users routinely mis-case
        // EditorIDs and the game treats them case-insensitively at
        // runtime, so Mora follows suit.
        std::string lc = to_lower(ref.editor_id);
        for (const auto& [k, v] : editor_ids) {
            if (to_lower(k) == lc) {
                out_formid = v;
                return ResolveError::None;
            }
        }
        return ResolveError::MissingEditorId;
    }

    // FormID ref: `0xNNNN~Plugin.ext`.
    if (!plugin_runtime_index) return ResolveError::FormidUnsupported;
    std::string key = to_lower(ref.mod_file);
    auto it = plugin_runtime_index->find(key);
    if (it == plugin_runtime_index->end()) return ResolveError::MissingPlugin;
    out_formid = mora::ext::globalize_formid(it->second, ref.formid);
    return ResolveError::None;
}

std::string ref_to_string(const KidRef& ref) {
    if (ref.is_editor_id()) return ref.editor_id;
    return fmt::format("0x{:X}~{}", ref.formid, ref.mod_file);
}

void emit_diag(mora::DiagBag& diags, const std::string& code,
               const std::string& msg, const KidFile& file, int line_num) {
    mora::SourceSpan span{file.path.string(), static_cast<uint32_t>(line_num), 0,
                          static_cast<uint32_t>(line_num), 0};
    diags.warning(code, msg, span, /*source_line*/ "");
}

} // namespace

std::vector<KidFactEmission> resolve_kid_file(
    const KidFile&                                     file,
    const std::unordered_map<std::string, uint32_t>&   editor_ids,
    const std::unordered_map<std::string, uint32_t>*   plugin_runtime_index,
    mora::StringPool&                                  pool,
    mora::DiagBag&                                     diags,
    uint32_t&                                          next_rule_id) {

    std::vector<KidFactEmission> out;

    // Surface any parser-level diagnostics up to the caller.
    for (const auto& pd : file.diags) {
        emit_diag(diags, "kid-parse",
                  fmt::format("{}:{}: {}", file.path.filename().string(), pd.line, pd.message),
                  file, pd.line);
    }

    auto err_code = [](ResolveError e) -> const char* {
        switch (e) {
            case ResolveError::FormidUnsupported: return "kid-formid-unsupported";
            case ResolveError::MissingPlugin:     return "kid-missing-plugin";
            case ResolveError::MissingEditorId:   return "kid-unresolved";
            case ResolveError::None:              break;
        }
        return "kid-unresolved";
    };

    for (const auto& line : file.lines) {
        // Resolve target keyword first — if this fails the whole line is dropped.
        uint32_t target_fid = 0;
        auto err = resolve_ref(line.target, editor_ids, plugin_runtime_index, target_fid);
        if (err != ResolveError::None) {
            emit_diag(diags, err_code(err),
                fmt::format("KID target \"{}\" unresolved; line dropped",
                            ref_to_string(line.target)),
                file, line.source_line);
            continue;
        }

        // Gather resolved filter values. For v1 we flatten AND-groups
        // (KID's '+' operator) into OR across rows — a narrower filter
        // than KID would give in the AND case, but strictly a subset of
        // its match set, so no false positives.
        std::vector<uint32_t> filter_values;
        std::vector<std::string> dropped_filter;
        for (const auto& group : line.filter) {
            for (const auto& ref : group.values) {
                uint32_t fid = 0;
                auto ferr = resolve_ref(ref, editor_ids, plugin_runtime_index, fid);
                if (ferr == ResolveError::None) filter_values.push_back(fid);
                else dropped_filter.push_back(ref_to_string(ref));
            }
        }
        if (!dropped_filter.empty()) {
            std::string joined;
            for (size_t i = 0; i < dropped_filter.size(); ++i) {
                if (i) joined += ", ";
                joined += dropped_filter[i];
            }
            emit_diag(diags, "kid-unresolved",
                fmt::format("KID filter values unresolved on line {}: [{}]; narrowed filter kept",
                            line.source_line, joined),
                file, line.source_line);
        }

        // Drop the whole line if its (positive) filter ended up empty
        // only because every value was unresolvable. A legitimately
        // empty filter (NONE) has line.filter.empty() — we don't drop
        // those, they mean "all items of the type."
        if (!line.filter.empty() && filter_values.empty()) {
            emit_diag(diags, "kid-unresolved",
                fmt::format("KID filter for line {} lost all values; line dropped",
                            line.source_line),
                file, line.source_line);
            continue;
        }

        // Allocate RuleID and emit facts.
        uint32_t rule_id = next_rule_id++;

        KidFactEmission dist;
        dist.relation = "ini/kid_dist";
        dist.values   = {
            mora::Value::make_int(static_cast<int64_t>(rule_id)),
            mora::Value::make_formid(target_fid),
            mora::Value::make_string(pool.intern(line.item_type)),
        };
        out.push_back(std::move(dist));

        for (uint32_t fv : filter_values) {
            KidFactEmission f;
            f.relation = "ini/kid_filter";
            f.values   = {
                mora::Value::make_int(static_cast<int64_t>(rule_id)),
                mora::Value::make_string(pool.intern("keyword")),
                mora::Value::make_formid(fv),
            };
            out.push_back(std::move(f));
        }

        for (const auto& trait : line.traits) {
            // Only E / -E are recognized in v1. Others are silently
            // dropped here — the parser already surfaced them.
            if (trait != "E" && trait != "-E") continue;
            KidFactEmission t;
            t.relation = "ini/kid_trait";
            t.values   = {
                mora::Value::make_int(static_cast<int64_t>(rule_id)),
                mora::Value::make_string(pool.intern(trait)),
            };
            out.push_back(std::move(t));
        }

        // Chance <100 is reported but not enforced at compile time.
        if (line.chance < 100.0) {
            emit_diag(diags, "kid-chance-ignored",
                fmt::format("KID line {} has chance={}; compile-time applies it unconditionally",
                            line.source_line, line.chance),
                file, line.source_line);
        }
    }

    return out;
}

} // namespace mora_skyrim_compile
