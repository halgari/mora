#pragma once

#include "mora/core/string_pool.h"

namespace mora {

class FactDB;
class ResolvedPatchSet;

// Walks a ResolvedPatchSet and populates the four Skyrim effect
// relations (skyrim/set, skyrim/add, skyrim/remove, skyrim/multiply)
// with `(FormID, :FieldName, Value)` tuples.
//
// Values are carried through as their typed Value form (FormID, Int,
// Float, String, Bool) because ResolvedPatchSet preserves StringPool
// references on String values — unlike PatchBuffer's byte-packed
// uint64 encoding, which requires the binary-patch StringTable to
// decode and motivated a workaround skip in earlier plans.
//
// Relations are configured lazily on first emit per relation (arity 3,
// column 0 indexed on the FormID).
void populate_effect_facts(const ResolvedPatchSet& patches,
                            FactDB& db,
                            StringPool& pool);

} // namespace mora
