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
    // skyrim/add(X, :Keyword, <target>) where <target> is either an
    // EditorIdExpr (for EditorID-shaped KID refs) or a TaggedLiteralExpr
    // with tag "form" (for `0xFFF~Mod.esp` refs — the `#form` reader
    // globalizes them to FormIdLiteral during reader expansion).
    std::vector<mora::Rule> rules;
};

// Convert a parsed KID file into Mora rules.
//
// `editor_ids` maps EditorID -> (runtime-globalized) FormID; produced
// by SkyrimEspDataSource and passed through via LoadCtx. Used only for
// EditorID-shaped KID refs (exact + case-insensitive match) and
// wildcard expansion.
//
// FormID refs (`0xFFF~Mod.esp`) compile into TaggedLiteralExpr nodes
// that the `#form` reader globalizes in the reader-expansion phase —
// no plugin-runtime-index lookup happens in this resolver.
//
// Resolution policy:
//   - EditorID references: looked up in `editor_ids` (case-insensitive).
//     Misses emit `kid-unresolved` and drop the line.
//   - FormID references: emitted as TaggedLiteralExpr("form",
//     "0xNNN@Plugin.ext"). Any missing-plugin / data-less diagnostics
//     surface later from the `#form` reader.
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
    mora::StringPool&                                    pool,
    mora::DiagBag&                                       diags);

} // namespace mora_skyrim_compile
