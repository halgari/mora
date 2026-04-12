#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

namespace mora {

// Build a Module containing generic SPID/KID distribution rules.
// These rules join spid_dist/kid_dist facts against the form DB.
//
// Generates filtered rules (by keyword/form) and no-filter variants
// that match the "none" marker emitted for rules with no filters.
Module build_ini_distribution_rules(StringPool& pool);

} // namespace mora
