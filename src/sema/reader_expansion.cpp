#include "mora/sema/reader_expansion.h"

#include <string>
#include <variant>

namespace mora {

namespace {

// Forward declarations — the walk is mutually recursive across Expr
// and Clause shapes.
void expand_expr(Expr& e,
                  const mora::ext::ExtensionContext& ext_ctx,
                  mora::ext::ReaderContext& rctx);

void expand_clause(Clause& c,
                    const mora::ext::ExtensionContext& ext_ctx,
                    mora::ext::ReaderContext& rctx);

void expand_fact_pattern(FactPattern& fp,
                          const mora::ext::ExtensionContext& ext_ctx,
                          mora::ext::ReaderContext& rctx) {
    for (Expr& arg : fp.args) expand_expr(arg, ext_ctx, rctx);
}

void expand_expr(Expr& e,
                  const mora::ext::ExtensionContext& ext_ctx,
                  mora::ext::ReaderContext& rctx) {
    // Handle TaggedLiteralExpr up front — replace the whole node if a
    // reader is registered, otherwise diag and leave in place.
    if (auto const* tl = std::get_if<TaggedLiteralExpr>(&e.data)) {
        std::string const tag_name    = std::string(rctx.pool.get(tl->tag));
        std::string const payload_str = std::string(rctx.pool.get(tl->payload));
        SourceSpan const span         = tl->span;

        const mora::ext::ReaderFn* fn = ext_ctx.find_reader(tag_name);
        if (fn == nullptr) {
            rctx.diags.error(
                "reader-unknown-tag",
                std::string("no reader registered for tag '#") + tag_name + "'",
                span, "");
            return;
        }
        Expr replaced = (*fn)(rctx, payload_str, span);
        e = std::move(replaced);
        // Recursively expand the replacement — readers may produce
        // expressions that themselves contain tagged literals (rare
        // but legal; avoids a surprise if a reader composes).
        expand_expr(e, ext_ctx, rctx);
        return;
    }

    // Recurse into composite expression shapes.
    if (auto* be = std::get_if<BinaryExpr>(&e.data)) {
        if (be->left)  expand_expr(*be->left,  ext_ctx, rctx);
        if (be->right) expand_expr(*be->right, ext_ctx, rctx);
        return;
    }
    if (auto* ce = std::get_if<CallExpr>(&e.data)) {
        for (Expr& arg : ce->args) expand_expr(arg, ext_ctx, rctx);
        return;
    }
    // Leaf nodes (Variable, Literal*, EditorId, Symbol, Discard,
    // FormIdLiteral): nothing to walk into.
}

void expand_clause(Clause& c,
                    const mora::ext::ExtensionContext& ext_ctx,
                    mora::ext::ReaderContext& rctx) {
    std::visit([&](auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, FactPattern>) {
            expand_fact_pattern(node, ext_ctx, rctx);
        } else if constexpr (std::is_same_v<T, GuardClause>) {
            if (node.expr) expand_expr(*node.expr, ext_ctx, rctx);
        } else if constexpr (std::is_same_v<T, OrClause>) {
            for (auto& branch : node.branches) {
                for (FactPattern& fp : branch) {
                    expand_fact_pattern(fp, ext_ctx, rctx);
                }
            }
        } else if constexpr (std::is_same_v<T, InClause>) {
            if (node.variable) expand_expr(*node.variable, ext_ctx, rctx);
            for (Expr& v : node.values) expand_expr(v, ext_ctx, rctx);
        }
    }, c.data);
}

} // namespace

void expand_readers(Module&                            mod,
                     const mora::ext::ExtensionContext& ext_ctx,
                     mora::ext::ReaderContext&          rctx) {
    for (Rule& rule : mod.rules) {
        for (Expr& ha : rule.head_args) {
            expand_expr(ha, ext_ctx, rctx);
        }
        for (Clause& clause : rule.body) {
            expand_clause(clause, ext_ctx, rctx);
        }
    }
}

} // namespace mora
