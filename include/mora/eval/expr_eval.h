#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/data/value.h"

#include <unordered_map>

namespace mora {

using Bindings = std::unordered_map<uint32_t, Value>;

// Pure-functional expression evaluation. Takes all state it needs
// explicitly. Handles VariableExpr, SymbolExpr, literals, BinaryExpr,
// CallExpr, FieldAccessExpr, EditorIdExpr.
//
// If `bindings` doesn't have a variable or `symbols` doesn't have a symbol,
// returns Value::make_var().
Value resolve_expr(const Expr& e,
                    const Bindings& bindings,
                    StringPool& pool,
                    const std::unordered_map<uint32_t, uint32_t>& symbols);

// Boolean coercion of resolve_expr for guards.
// Evaluates the expression and returns true iff the result is truthy
// (compares Int/Float/FormID values with BinaryExpr comparison operators).
bool evaluate_guard(const Expr& e,
                     const Bindings& bindings,
                     StringPool& pool,
                     const std::unordered_map<uint32_t, uint32_t>& symbols);

} // namespace mora
