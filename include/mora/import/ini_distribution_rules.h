#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

namespace mora {

// Build a Module containing generic SPID/KID distribution rules.
// These rules join spid_dist/kid_dist facts against the form DB.
//
// Currently generates ~20 rules that replace thousands of individually
// generated rules from INI files.
//
// Known limitation: no-filter variants (rules without spid_filter/kid_filter)
// are not generated because negation (not spid_filter(RuleID, _, _)) support
// is complex. Rules without filters will produce no patches.
Module build_ini_distribution_rules(StringPool& pool);

} // namespace mora
