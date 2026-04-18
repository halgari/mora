#pragma once

#include "mora/eval/patch_set.h"  // for FieldId
#include <string>

namespace mora {

// Returns the canonical field name for a FieldId enum value, e.g.
// FieldId::GoldValue -> "GoldValue". Used as the keyword payload for
// effect-fact emission and by CLI display paths.
std::string field_id_name(FieldId id);

} // namespace mora
