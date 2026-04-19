#pragma once

#include "mora_skyrim_compile/kid_parser.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace mora {
class DiagBag;
}

namespace mora_skyrim_compile {

// One (relation, tuple) pair ready for FactDB::add_fact. `relation` is
// a string; the caller interns it into its own pool. Keeping strings
// here lets the resolver stay pool-agnostic.
struct KidFactEmission {
    std::string  relation;
    mora::Tuple  values;
};

// Convert a parsed KID file into fact tuples.
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
// `next_rule_id` is updated in-place; each line that resolves cleanly
// consumes exactly one RuleID.
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
//   - Exclude with unresolvable values: the line is dropped — a
//     weakened exclude would over-distribute.
std::vector<KidFactEmission> resolve_kid_file(
    const KidFile&                                       file,
    const std::unordered_map<std::string, uint32_t>&     editor_ids,
    const std::unordered_map<std::string, uint32_t>*     plugin_runtime_index,
    mora::StringPool&                                    pool,
    mora::DiagBag&                                       diags,
    uint32_t&                                            next_rule_id);

} // namespace mora_skyrim_compile
