#include "mora_skyrim_compile/kid_resolver.h"

#include "mora_skyrim_compile/kid_rule_builder.h"
#include "mora_skyrim_compile/kid_util.h"

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

// Per-wildcard match-count warning threshold. Distinct from the hard cap
// above: this fires as an informational warning when a single glob
// touches a lot of the EditorID map, while the cap is the limit on
// AND-group cross-products.
constexpr size_t kWildcardFanoutWarnThreshold = 1000;

// Supported KID trait tokens → body-clause evidence they compile into.
// Adding a new trait is one entry here (and, once range/constant-arg
// traits land, a corresponding extension to TraitEvidence). The `-`
// polarity prefix is stripped by the caller; both `E` and `-E` look up
// the same entry and only differ in TraitRef::negated.
const std::unordered_map<std::string_view, TraitEvidence>&
kid_trait_token_map() {
    static const std::unordered_map<std::string_view, TraitEvidence> m = {
        {"E", {"form", "enchanted_with", 1}},
    };
    return m;
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
    MissingEditorId,  // caller's editor_ids doesn't contain this name
};

// Resolve a single KID ref to a ResolvedRef. EditorID refs look up the
// canonical casing in `editor_ids`; FormID refs (`0xNNN~Plugin.ext`)
// become a TaggedLiteralExpr payload (`"0xNNN@Plugin.ext"`) that the
// `#form` reader globalizes during the reader-expansion phase — no
// plugin-runtime-index lookup happens here.
ResolveError resolve_ref(
    const KidRef&                                     ref,
    const std::unordered_map<std::string, uint32_t>&  editor_ids,
    ResolvedRef&                                      out) {
    if (ref.is_editor_id()) {
        // Exact match first — ESP extraction preserves casing as-written.
        auto it = editor_ids.find(ref.editor_id);
        if (it != editor_ids.end()) {
            out = ResolvedRef::make_editor_id(it->first);
            return ResolveError::None;
        }
        // Case-insensitive fallback. KID users routinely mis-case
        // EditorIDs and the game treats them case-insensitively at
        // runtime, so Mora follows suit.
        std::string lc = to_lower(ref.editor_id);
        for (const auto& [k, v] : editor_ids) {
            if (to_lower(k) == lc) {
                out = ResolvedRef::make_editor_id(k);
                return ResolveError::None;
            }
        }
        return ResolveError::MissingEditorId;
    }

    // FormID ref: `0xNNN~Plugin.ext`. Emit a TaggedLiteralExpr payload
    // — the `#form` reader handles plugin-index lookup, globalization,
    // and missing-plugin / data-less diagnostics during expansion.
    out = ResolvedRef::make_tagged_form(
        fmt::format("0x{:X}@{}", ref.formid, ref.mod_file));
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
    mora::StringPool&                                  pool,
    mora::DiagBag&                                     diags) {

    KidResolveResult out;

    // Surface any parser-level diagnostics up to the caller.
    for (const auto& pd : file.diags) {
        emit_diag(diags, "kid-parse",
                  fmt::format("{}:{}: {}", file.path.filename().string(), pd.line, pd.message),
                  file, pd.line);
    }

    for (const auto& line : file.lines) {
        // Resolve target keyword first — if this fails the whole line is dropped.
        ResolvedRef target;
        auto err = resolve_ref(line.target, editor_ids, target);
        if (err != ResolveError::None) {
            emit_diag(diags, "kid-unresolved",
                fmt::format("KID target \"{}\" unresolved; line dropped",
                            ref_to_string(line.target)),
                file, line.source_line);
            continue;
        }

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
                    if (matches.size() > kWildcardFanoutWarnThreshold) {
                        emit_diag(diags, "kid-wildcard-fanout",
                            fmt::format("KID line {}: wildcard \"{}\" expanded to {} matches",
                                        line.source_line, ref.editor_id, matches.size()),
                            file, line.source_line);
                    }
                    m.alternatives.reserve(matches.size());
                    for (auto& [edid, fid] : matches) {
                        (void)fid;  // wildcards produce EditorIdExpr nodes;
                                    // evaluator resolves to FormID at scan time.
                        m.alternatives.push_back(
                            ResolvedRef::make_editor_id(std::move(edid)));
                    }
                    members.push_back(std::move(m));
                    continue;
                }
                ResolvedRef r;
                auto ferr = resolve_ref(ref, editor_ids, r);
                if (ferr == ResolveError::None) {
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

        // Trait recognition via the trait-token map (defined at
        // namespace scope above). Each supported token maps to a
        // TraitEvidence describing the body clause it compiles into; a
        // leading `-` flips polarity. Unknown tokens are dropped with a
        // diagnostic — either typos or traits Mora doesn't support yet.
        std::vector<TraitRef> resolved_traits;
        for (const auto& raw : line.traits) {
            std::string_view tok = raw;
            bool const negated = !tok.empty() && tok.front() == '-';
            if (negated) tok.remove_prefix(1);
            auto it = kid_trait_token_map().find(tok);
            if (it == kid_trait_token_map().end()) {
                emit_diag(diags, "kid-unknown-trait",
                    fmt::format("KID line {}: trait \"{}\" not supported; dropped",
                                line.source_line, raw),
                    file, line.source_line);
                continue;
            }
            resolved_traits.push_back({it->second, negated});
        }

        // Source span for diagnostics on the synthesized AST nodes.
        mora::SourceSpan span;
        span.file = file.path.string();
        span.start_line = static_cast<uint32_t>(line.source_line);
        span.end_line   = static_cast<uint32_t>(line.source_line);

        auto built = build_rules(target, line.item_type, resolved_groups,
                                  resolved_traits, span, pool);
        for (auto& r : built) out.rules.push_back(std::move(r));

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
