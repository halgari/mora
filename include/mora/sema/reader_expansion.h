#pragma once

#include "mora/ast/ast.h"
#include "mora/ext/extension.h"

namespace mora {

// Walks every Expr in `mod` (rule heads, fact-pattern args, guard
// expressions, in-clause variables and values, binary and call-expr
// children) and replaces any TaggedLiteralExpr with whatever the
// tag's registered reader returns. Tags without a registered reader
// produce a `reader-unknown-tag` diagnostic and the node is left in
// place (evaluation will treat it as an unbound var).
//
// Expected to run AFTER ESP-load (readers typically need the editor-id
// map or plugin runtime index) and BEFORE name-resolution.
void expand_readers(Module&                          mod,
                     const mora::ext::ExtensionContext& ext_ctx,
                     mora::ext::ReaderContext&        rctx);

} // namespace mora
