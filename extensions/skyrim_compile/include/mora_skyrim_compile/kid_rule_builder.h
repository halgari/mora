#pragma once

#include "mora/ast/ast.h"
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mora_skyrim_compile {

// One filter / target value after C++-side resolution. `formid` is the
// final 32-bit runtime FormID; `editor_id` is the name to embed as an
// EditorIdExpr in the synthesized AST. For lines that referenced an
// EditorID directly this is the original name; for FormID references
// (`0xFFF~Mod.esp`) it's a synthetic name like `__kid_formid_0a000800`
// — see compose_synthetic_editor_id.
struct ResolvedRef {
    uint32_t    formid = 0;
    std::string editor_id;
};

// Compose a deterministic synthetic EditorID name for a resolved
// FormID. Used so FormID-only references become EditorIdExpr nodes
// that the evaluator's symbol table resolves uniformly with real
// EditorIDs. The chosen prefix can never collide with a real Skyrim
// EditorID (those start with a letter).
std::string compose_synthetic_editor_id(uint32_t formid);

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
//     [if trait_e:     form/enchanted_with(X, _anonN)]
//     [if trait_neg_e: not form/enchanted_with(X, _anonN)]
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
    bool                                                   trait_e,
    bool                                                   trait_neg_e,
    const mora::SourceSpan&                                source_span,
    mora::StringPool&                                      pool);

} // namespace mora_skyrim_compile
