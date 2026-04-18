#pragma once

#include "mora/core/string_pool.h"

namespace mora {

class FactDB;
class PatchBuffer;

// Walks a PatchBuffer's entries and populates the four Skyrim effect
// relations (skyrim/set, skyrim/add, skyrim/remove, skyrim/multiply)
// with `(FormID, :FieldName, Value)` tuples. Configures the relations
// lazily on first use.
void populate_effect_facts(const PatchBuffer& buf,
                            FactDB& db,
                            StringPool& pool);

} // namespace mora
