#include "mora_skyrim_compile/kid_resolver.h"

#include "mora_skyrim_compile/kid_rule_builder.h"

#include "mora/core/source_location.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/runtime_index.h"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>

namespace mora_skyrim_compile {

namespace {

// Cap on rules synthesized per single AND-group after wildcard
// cross-product expansion. A KID author writing `*Iron+*Heavy` against
// a Wabbajack-scale corpus could otherwise spawn millions of rules.
constexpr size_t kMaxAndGroupExpansion = 1024;

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

// Enumerate (EditorID, FormID) entries matching `pattern`. Linear scan
// — the map is typically 50k-500k entries; one pass per wildcard is
// acceptable. Returns {} for degenerate `*` / empty patterns.
std::vector<std::pair<std::string, uint32_t>> expand_glob(
    std::string_view pattern,
    const std::unordered_map<std::string, uint32_t>& editor_ids) {
    std::vector<std::pair<std::string, uint32_t>> out;
    if (pattern.empty()) return out;
    if (pattern.size() == 1 && pattern[0] == '*') return out;
    out.reserve(16);
    for (const auto& [edid, fid] : editor_ids) {
        if (glob_match_ci(pattern, edid)) out.emplace_back(edid, fid);
    }
    // Sort by EditorID for deterministic rule ordering across runs.
    std::sort(out.begin(), out.end());
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
    uint32_t&                                         out_formid,
    std::string&                                      out_canonical_editor_id) {
    if (ref.is_editor_id()) {
        // Exact match first — ESP extraction preserves casing as-written.
        auto it = editor_ids.find(ref.editor_id);
        if (it != editor_ids.end()) {
            out_formid = it->second;
            out_canonical_editor_id = it->first;
            return ResolveError::None;
        }
        // Case-insensitive fallback. KID users routinely mis-case
        // EditorIDs and the game treats them case-insensitively at
        // runtime, so Mora follows suit.
        std::string lc = to_lower(ref.editor_id);
        for (const auto& [k, v] : editor_ids) {
            if (to_lower(k) == lc) {
                out_formid = v;
                out_canonical_editor_id = k;
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
    // No real EditorID for a FormID ref — synthesize a stable one so
    // the AST has something to put in EditorIdExpr. Callers register
    // it as a synthetic_editor_ids entry so the evaluator's symbol
    // table resolves it to `out_formid`.
    out_canonical_editor_id = compose_synthetic_editor_id(out_formid);
    return ResolveError::None;
}

std::string ref_to_string(const KidRef& ref) {
    if (ref.is_editor_id()) return ref.editor_id;
    return fmt::format("0x{:X}~{}", ref.formid, ref.mod_file);
}

void emit_diag(mora::DiagBag& diags, const std::string& code,
               const std::string& msg, const KidFile& file, int line_num) {
    mora::SourceSpan span;
    span.file = file.path.string();
    span.start_line = static_cast<uint32_t>(line_num);
    span.end_line   = static_cast<uint32_t>(line_num);
    diags.warning(code, msg, span, /*source_line*/ "");
}

// One member of an AND-group after individual-ref resolution: a list
// of alternatives. For a literal EditorID this is a 1-element list;
// for a wildcard it's the glob expansion.
struct ResolvedMember {
    std::vector<ResolvedRef> alternatives;
    // Diagnostic shorthand if every alternative failed to resolve and
    // the member should be dropped. Non-empty member.alternatives means
    // at least one survivor.
    bool dropped = false;
};

// Cross-product of N member alternatives → list of AND-tuples.
// {[a, b], [c]} → {[a, c], [b, c]}. Cap honored externally.
void cartesian_extend(
    std::vector<std::vector<ResolvedRef>>& acc,
    const std::vector<ResolvedRef>& next_alternatives)
{
    if (acc.empty()) {
        acc.reserve(next_alternatives.size());
        for (const auto& alt : next_alternatives) {
            acc.push_back({alt});
        }
        return;
    }
    std::vector<std::vector<ResolvedRef>> grown;
    grown.reserve(acc.size() * next_alternatives.size());
    for (const auto& tuple : acc) {
        for (const auto& alt : next_alternatives) {
            std::vector<ResolvedRef> extended = tuple;
            extended.push_back(alt);
            grown.push_back(std::move(extended));
        }
    }
    acc = std::move(grown);
}

} // namespace

KidResolveResult resolve_kid_file(
    const KidFile&                                     file,
    const std::unordered_map<std::string, uint32_t>&   editor_ids,
    const std::unordered_map<std::string, uint32_t>*   plugin_runtime_index,
    mora::StringPool&                                  pool,
    mora::DiagBag&                                     diags) {

    KidResolveResult out;

    // Track synthetic editor IDs we need to register with the evaluator.
    // De-dup with a set so a FormID referenced from multiple lines only
    // yields one entry.
    std::unordered_map<std::string, uint32_t> synth_seen;
    auto record_synthetic = [&](const std::string& edid, uint32_t fid) {
        // Only emit for our own synthetic prefix — real EditorIDs come
        // from editor_ids and don't need re-registration.
        if (edid.rfind("__kid_formid_", 0) != 0) return;
        synth_seen.try_emplace(edid, fid);
    };

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
        ResolvedRef target;
        auto err = resolve_ref(line.target, editor_ids, plugin_runtime_index,
                                target.formid, target.editor_id);
        if (err != ResolveError::None) {
            emit_diag(diags, err_code(err),
                fmt::format("KID target \"{}\" unresolved; line dropped",
                            ref_to_string(line.target)),
                file, line.source_line);
            continue;
        }
        record_synthetic(target.editor_id, target.formid);

        // Resolve every filter group, expanding wildcards and crossing
        // the resulting per-member alternative lists.
        //
        // Per-group resolution policy (preserved from v1):
        //   - A group where EVERY value fails resolves to no AND-tuples
        //     and is dropped entirely.
        //   - A group with SOME failures yields a narrower (fewer
        //     conjuncts) intent and emits `kid-unresolved` for the
        //     dropped values.
        // New in v2 (rule-synthesis): wildcards inside AND-groups are
        // expanded and cross-producted with the other members. The
        // earlier `kid-wildcard-in-and` restriction is no longer needed.
        std::vector<std::vector<ResolvedRef>> resolved_groups;
        std::vector<std::string> dropped_filter;
        for (const auto& group : line.filter) {
            std::vector<ResolvedMember> members;
            members.reserve(group.values.size());
            for (const auto& ref : group.values) {
                ResolvedMember m;
                if (ref.wildcard) {
                    auto matches = expand_glob(ref.editor_id, editor_ids);
                    if (matches.empty()) {
                        const char* code = (ref.editor_id == "*")
                            ? "kid-wildcard-all" : "kid-wildcard-empty";
                        emit_diag(diags, code,
                            fmt::format("KID line {}: wildcard \"{}\" matched no EditorIDs; value dropped",
                                        line.source_line, ref.editor_id),
                            file, line.source_line);
                        m.dropped = true;
                        members.push_back(std::move(m));
                        continue;
                    }
                    if (matches.size() > 1000) {
                        emit_diag(diags, "kid-wildcard-fanout",
                            fmt::format("KID line {}: wildcard \"{}\" expanded to {} matches",
                                        line.source_line, ref.editor_id, matches.size()),
                            file, line.source_line);
                    }
                    m.alternatives.reserve(matches.size());
                    for (auto& [edid, fid] : matches) {
                        ResolvedRef r;
                        r.formid    = fid;
                        r.editor_id = std::move(edid);
                        m.alternatives.push_back(std::move(r));
                    }
                    members.push_back(std::move(m));
                    continue;
                }
                ResolvedRef r;
                auto ferr = resolve_ref(ref, editor_ids, plugin_runtime_index,
                                          r.formid, r.editor_id);
                if (ferr == ResolveError::None) {
                    record_synthetic(r.editor_id, r.formid);
                    m.alternatives.push_back(std::move(r));
                } else {
                    dropped_filter.push_back(ref_to_string(ref));
                    m.dropped = true;
                }
                members.push_back(std::move(m));
            }

            // Drop members with no surviving alternatives, then take
            // the cross-product across the remaining members.
            std::vector<std::vector<ResolvedRef>> tuples;
            bool any_member_kept = false;
            for (const auto& m : members) {
                if (m.alternatives.empty()) continue;
                any_member_kept = true;
                cartesian_extend(tuples, m.alternatives);
                if (tuples.size() > kMaxAndGroupExpansion) {
                    emit_diag(diags, "kid-wildcard-fanout",
                        fmt::format("KID line {}: AND-group cross-product exceeded {} entries; group dropped",
                                    line.source_line, kMaxAndGroupExpansion),
                        file, line.source_line);
                    tuples.clear();
                    any_member_kept = false;
                    break;
                }
            }
            if (!any_member_kept) continue;
            for (auto& t : tuples) resolved_groups.push_back(std::move(t));
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

        // Trait recognition. v1 wires E / -E only; other tokens stay
        // silently dropped (the parser surfaces unknown traits).
        bool trait_e = false;
        bool trait_neg_e = false;
        for (const auto& trait : line.traits) {
            if (trait == "E") trait_e = true;
            else if (trait == "-E") trait_neg_e = true;
        }

        // Source span for diagnostics on the synthesized AST nodes.
        mora::SourceSpan span;
        span.file = file.path.string();
        span.start_line = static_cast<uint32_t>(line.source_line);
        span.end_line   = static_cast<uint32_t>(line.source_line);

        auto built = build_rules(target, line.item_type, resolved_groups,
                                  trait_e, trait_neg_e, span, pool);
        for (auto& r : built) out.rules.push_back(std::move(r));

        // Chance <100 is reported but not enforced at compile time.
        if (line.chance < 100.0) {
            emit_diag(diags, "kid-chance-ignored",
                fmt::format("KID line {} has chance={}; compile-time applies it unconditionally",
                            line.source_line, line.chance),
                file, line.source_line);
        }
    }

    out.synthetic_editor_ids.reserve(synth_seen.size());
    for (auto& kv : synth_seen) {
        out.synthetic_editor_ids.emplace_back(kv.first, kv.second);
    }
    return out;
}

} // namespace mora_skyrim_compile
