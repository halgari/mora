#pragma once

#include "mora/ast/ast.h"
#include "mora/core/source_location.h"
#include "mora/ext/extension.h"

#include <string_view>

namespace mora_skyrim_compile {

// Reader for `#form "<payload>"`. Accepted payload shapes:
//
//   "0xNNN@Plugin.ext"   — globalized via ReaderContext::plugin_runtime_index,
//                           returned as a FormIdLiteral.
//   "EditorID"           — returned verbatim as an EditorIdExpr (the
//                           evaluator does case-insensitive symbol lookup).
//
// Errors (malformed hex, missing plugin, empty payload) produce a diag
// and return a DiscardExpr placeholder so downstream passes don't crash.
mora::Expr form_reader(mora::ext::ReaderContext& ctx,
                        std::string_view        payload,
                        const mora::SourceSpan& span);

} // namespace mora_skyrim_compile
