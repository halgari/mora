#pragma once

#include "mora_skyrim_compile/kid_parser.h"

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mora {
class DiagBag;
}

namespace mora_skyrim_compile {

// Output of resolving one KID file.
struct KidResolveResult {
    // Synthesized rules — one per KID line per OR-group (per AND-group
    // after wildcard cross-product expansion). All have head shape
    // skyrim/add(X, :Keyword, @TargetEditorId).
    std::vector<mora::Rule> rules;

    // Synthetic EditorID names (for FormID-only references like
    // `0xFFF~Mod.esp`) that need registering in the evaluator's symbol
    // table so the EditorIdExpr nodes in `rules` resolve to the right
    // FormIDs. The caller merges these into LoadCtx::editor_ids_out (or
    // calls Evaluator::set_symbol_formid directly) before evaluation.
    std::vector<std::pair<std::string, uint32_t>> synthetic_editor_ids;
};

// Convert a parsed KID file into Mora rules.
//
// `editor_ids` maps EditorID -> (runtime-globalized) FormID; produced
// by SkyrimEspDataSource and passed through via LoadCtx.
//
// `plugin_runtime_index` maps lowercase plugin filename -> packed
// runtime-index descriptor (see mora/ext/runtime_index.h). When
// non-null, KID `0xNNN~Plugin.ext` references resolve via
// `mora::ext::globalize_formid`; when null, those references are
// rejected with `kid-formid-unsupported` (back-compat v1 behavior).
//
// Resolution policy:
//   - EditorID references: looked up in `editor_ids` (case-insensitive).
//     Misses emit `kid-unresolved` and drop the line.
//   - FormID references (`0xFFF~Mod.esp`): resolved via
//     `plugin_runtime_index` when available. An unknown plugin
//     produces `kid-missing-plugin`; an unavailable map produces
//     `kid-formid-unsupported`. Both drop the line.
//   - Partial filters: if the target resolves but some filter values
//     don't, resolvable values are kept and a warning records the
//     drops; the line is retained (narrower filter still matches a
//     subset of the original intent).
//   - Wildcards inside an AND-group: expanded and cross-producted with
//     other members of the group. Each resulting AND-tuple becomes its
//     own OR-alternative. Total fan-out is capped at
//     `kMaxAndGroupExpansion`; lines exceeding that drop the AND-group
//     with `kid-wildcard-fanout`.
KidResolveResult resolve_kid_file(
    const KidFile&                                       file,
    const std::unordered_map<std::string, uint32_t>&     editor_ids,
    const std::unordered_map<std::string, uint32_t>*     plugin_runtime_index,
    mora::StringPool&                                    pool,
    mora::DiagBag&                                       diags);

} // namespace mora_skyrim_compile
