#pragma once

#include "mora/ast/ast.h"
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mora_skyrim_compile {

// One filter / target value after C++-side resolution. Carries just
// enough to construct the right AST node at rule-build time: either
// an EditorIdExpr (for EditorID-shaped KID refs and wildcard matches)
// or a TaggedLiteralExpr with tag "form" (for `0xFFF~Mod.esp` refs —
// expanded to a FormIdLiteral by the `#form` reader).
//
// Only one field is non-empty at a time; use is_editor_id() / is_tagged_form()
// to dispatch. Stored as strings rather than AST nodes so this struct
// stays cheaply copyable.
struct ResolvedRef {
    std::string editor_id;       // non-empty iff this is an EditorID ref
    std::string tagged_payload;  // non-empty iff this is a #form ref
                                 //   (payload shape: "0xNNN@Plugin.ext")

    bool is_editor_id()   const { return !editor_id.empty(); }
    bool is_tagged_form() const { return !tagged_payload.empty(); }

    static ResolvedRef make_editor_id(std::string name) {
        ResolvedRef r;
        r.editor_id = std::move(name);
        return r;
    }
    static ResolvedRef make_tagged_form(std::string payload) {
        ResolvedRef r;
        r.tagged_payload = std::move(payload);
        return r;
    }
};

// Describes the body clause a trait compiles into. The KID resolver
// looks up each parsed trait token in a small table to get one of these,
// then hands a vector of (TraitEvidence, negated) pairs to build_rules.
// The builder itself does not special-case any specific trait — the
// table is the only place a new trait kind lands.
struct TraitEvidence {
    // Fact-pattern qualifier (e.g. "form").
    std::string_view qualifier;
    // Fact-pattern name (e.g. "enchanted_with").
    std::string_view name;
    // Number of fresh anonymous variables to append after the subject
    // `X`. For `form/enchanted_with(X, _)` this is 1.
    int              fresh_var_count = 0;
};

// Parsed trait token + polarity after resolver-side lookup. `negated`
// corresponds to a `-` prefix in the KID source (e.g. `-E`).
struct TraitRef {
    TraitEvidence evidence;
    bool          negated = false;
};

// Build the synthesized rules for one resolved KID line.
//
// One ResolvedKidLine produces N rules, where N is max(1, filter_groups.size()):
//   - If filter_groups is empty: one rule with no filter conjuncts.
//   - Otherwise: one rule per OR-group, each rule's body containing
//     all of that group's AND-members as positive form/keyword conjuncts.
//
// Every rule has identical head:
//     skyrim/add(X, :Keyword, @<target.editor_id>)
//
// Every rule's body has:
//     form/<item_type>(X)
//     [for each trait:  [not] <trait.qualifier>/<trait.name>(X, _freshN...)]
//     <filter conjuncts>
//
// `source_span` is attached to every constructed AST node for diagnostics.
//
// Returns an empty vector iff the inputs are degenerate (no item type,
// no target). Callers should check upstream and avoid calling with such.
std::vector<mora::Rule> build_rules(
    const ResolvedRef&                                     target,
    std::string_view                                       item_type,
    const std::vector<std::vector<ResolvedRef>>&           filter_groups,
    const std::vector<TraitRef>&                           traits,
    const mora::SourceSpan&                                source_span,
    mora::StringPool&                                      pool);

} // namespace mora_skyrim_compile
