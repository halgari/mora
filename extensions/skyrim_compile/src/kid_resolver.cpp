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

// Case-insensitive shell-style glob match. Supports '*' (zero or more
// chars) and '?' (exactly one). Iterative backtracker — simpler than
// converting to std::regex and avoids regex's pathological worst-case
// on patterns like `*a*b*c*`.
bool glob_match_ci(std::string_view pat, std::string_view text) {
    size_t i = 0, j = 0;
    size_t star = std::string_view::npos;
    size_t match = 0;
    while (j < text.size()) {
        if (i < pat.size() &&
            (pat[i] == '?' ||
             std::tolower(static_cast<unsigned char>(pat[i])) ==
                 std::tolower(static_cast<unsigned char>(text[j])))) {
            ++i; ++j;
        } else if (i < pat.size() && pat[i] == '*') {
            star = i++;
            match = j;
        } else if (star != std::string_view::npos) {
            i = star + 1;
            j = ++match;
        } else {
            return false;
        }
    }
    while (i < pat.size() && pat[i] == '*') ++i;
    return i == pat.size();
}

// Enumerate EditorIDs matching `pattern` in the given map. Linear scan
// — the map is typically 50k-500k entries; one pass per wildcard is
// acceptable (each takes ~a millisecond on commodity hardware).
// Returns {} for degenerate `*` / empty patterns; caller treats that
// as a diagnostic, not a silent success.
std::vector<uint32_t> expand_glob(
    std::string_view pattern,
    const std::unordered_map<std::string, uint32_t>& editor_ids) {
    std::vector<uint32_t> out;
    if (pattern.empty()) return out;
    // `*` alone would expand to every EditorID — almost always a bug.
    // Reject here; the caller emits kid-wildcard-all.
    if (pattern.size() == 1 && pattern[0] == '*') return out;
    out.reserve(16);
    for (const auto& [edid, fid] : editor_ids) {
        if (glob_match_ci(pattern, edid)) out.push_back(fid);
    }
    return out;
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

        // Gather resolved filter values per OR-group. KID's syntax is
        // an AND-of-ORs: values joined by ','   are separate groups
        // (OR'd); values joined by '+' within a group are AND'd. We
        // preserve that structure on emission so the stdlib wiring
        // rules can reconstruct "item satisfies ALL members of SOME
        // group" via negation-as-failure.
        //
        // Per-group resolution policy:
        //   - A group where EVERY value fails to resolve is dropped
        //     entirely (keeping an empty AND-group is meaningless).
        //   - A group where SOME values fail: the AND-conjunction is
        //     weakened (fewer required keywords), not a strict subset
        //     of original intent. Emit the survivors + a warning so
        //     the author sees the degradation. Alternative (drop the
        //     whole group) is stricter but usually wrong for
        //     Wabbajack-scale inputs where one missing mod shouldn't
        //     silently nuke an otherwise-fine rule.
        struct ResolvedGroup {
            std::vector<uint32_t> values;
        };
        std::vector<ResolvedGroup> resolved_groups;
        std::vector<std::string> dropped_filter;
        for (const auto& group : line.filter) {
            ResolvedGroup rg;
            for (const auto& ref : group.values) {
                if (ref.wildcard) {
                    // Wildcards weaken AND-group semantics — expanding
                    // `*Iron` inside `*Iron+ArmorHeavy` would mean "any
                    // weapon whose EditorID matches *Iron AND has
                    // ArmorHeavy", which v2 can't represent (the glob
                    // match happens pre-join, so once expanded the
                    // group becomes "has ANY of {IronSword, IronAxe,
                    // ...} AND ArmorHeavy" — wrong intent). Drop
                    // wildcards that appear inside multi-member AND
                    // groups and warn. Wildcards in solo-OR groups
                    // (the common case: `*Iron,*Gold`) expand cleanly.
                    if (group.values.size() > 1) {
                        emit_diag(diags, "kid-wildcard-in-and",
                            fmt::format("KID line {}: wildcard \"{}\" inside an AND-group is not supported; value dropped",
                                        line.source_line, ref.editor_id),
                            file, line.source_line);
                        continue;
                    }
                    auto matches = expand_glob(ref.editor_id, editor_ids);
                    if (matches.empty()) {
                        const char* code = (ref.editor_id == "*")
                            ? "kid-wildcard-all" : "kid-wildcard-empty";
                        emit_diag(diags, code,
                            fmt::format("KID line {}: wildcard \"{}\" matched no EditorIDs; value dropped",
                                        line.source_line, ref.editor_id),
                            file, line.source_line);
                        continue;
                    }
                    // Every glob match becomes its own OR-value in
                    // this group. Fan-out warning at a large
                    // threshold — too-broad wildcards degrade build
                    // time and almost always indicate a typo.
                    if (matches.size() > 1000) {
                        emit_diag(diags, "kid-wildcard-fanout",
                            fmt::format("KID line {}: wildcard \"{}\" expanded to {} matches",
                                        line.source_line, ref.editor_id, matches.size()),
                            file, line.source_line);
                    }
                    for (uint32_t fid : matches) rg.values.push_back(fid);
                    continue;
                }
                uint32_t fid = 0;
                auto ferr = resolve_ref(ref, editor_ids, plugin_runtime_index, fid);
                if (ferr == ResolveError::None) rg.values.push_back(fid);
                else dropped_filter.push_back(ref_to_string(ref));
            }
            if (!rg.values.empty()) resolved_groups.push_back(std::move(rg));
        }

        // Drop the whole line iff the ORIGINAL filter had groups and
        // we resolved NONE of them — the author wrote a filter that
        // means nothing in this load order.
        bool const had_filter = !line.filter.empty();
        bool const all_dropped = had_filter && resolved_groups.empty();
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

        if (all_dropped) {
            emit_diag(diags, "kid-unresolved",
                fmt::format("KID filter for line {} lost all groups; line dropped",
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

        // Emit one row per (group, member). GroupID is the group's
        // zero-based position within this line's filter — local to
        // RuleID, not global. AND-semantics within a group are handled
        // by the stdlib wiring rules via negation-as-failure (see
        // data/stdlib/kid.mora `_kid_group_matches`).
        for (size_t gi = 0; gi < resolved_groups.size(); ++gi) {
            for (uint32_t fv : resolved_groups[gi].values) {
                KidFactEmission f;
                f.relation = "ini/kid_filter";
                f.values   = {
                    mora::Value::make_int(static_cast<int64_t>(rule_id)),
                    mora::Value::make_int(static_cast<int64_t>(gi)),
                    mora::Value::make_string(pool.intern("keyword")),
                    mora::Value::make_formid(fv),
                };
                out.push_back(std::move(f));
            }
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
