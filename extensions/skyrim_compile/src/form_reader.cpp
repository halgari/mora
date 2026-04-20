#include "mora_skyrim_compile/form_reader.h"

#include "mora_skyrim_compile/kid_util.h"
#include "mora/ext/runtime_index.h"

#include <charconv>
#include <fmt/format.h>
#include <string>

namespace mora_skyrim_compile {

namespace {

mora::Expr make_discard(const mora::SourceSpan& span) {
    mora::Expr e;
    e.span = span;
    e.data = mora::DiscardExpr{span};
    return e;
}

mora::Expr make_form_id_literal(uint32_t value, const mora::SourceSpan& span) {
    mora::Expr e;
    e.span = span;
    e.data = mora::FormIdLiteral{value, span};
    return e;
}

mora::Expr make_editor_id(mora::StringPool& pool, std::string_view name,
                           const mora::SourceSpan& span) {
    mora::Expr e;
    e.span = span;
    e.data = mora::EditorIdExpr{pool.intern(name), span};
    return e;
}

} // namespace

mora::Expr form_reader(mora::ext::ReaderContext& ctx,
                        std::string_view         payload,
                        const mora::SourceSpan&  span) {
    if (payload.empty()) {
        ctx.diags.error("reader-form-empty",
            "#form requires a non-empty payload", span, "");
        return make_discard(span);
    }

    auto at = payload.find('@');
    if (at == std::string_view::npos) {
        // EditorID shape: `#form "MyKeyword"`. Emit an EditorIdExpr —
        // evaluator's symbol table does the case-insensitive lookup.
        // No immediate validation against editor_ids so `mora check` can
        // see the syntactic node even without --data-dir.
        return make_editor_id(ctx.pool, payload, span);
    }

    // FormID@Plugin shape.
    std::string_view const hex    = payload.substr(0, at);
    std::string_view const plugin = payload.substr(at + 1);
    if (hex.empty() || plugin.empty()) {
        ctx.diags.error("reader-form-malformed",
            fmt::format("#form: malformed payload \"{}\" "
                        "(expected \"0xHEX@Plugin.ext\")", payload),
            span, "");
        return make_discard(span);
    }

    // Strip optional "0x" / "0X" prefix.
    std::string_view hex_body = hex;
    if (hex_body.size() > 2 && hex_body[0] == '0'
        && (hex_body[1] == 'x' || hex_body[1] == 'X')) {
        hex_body.remove_prefix(2);
    }
    uint32_t local = 0;
    auto conv = std::from_chars(hex_body.data(),
                                  hex_body.data() + hex_body.size(),
                                  local, 16);
    if (conv.ec != std::errc{} || conv.ptr != hex_body.data() + hex_body.size()) {
        ctx.diags.error("reader-form-malformed",
            fmt::format("#form: \"{}\" is not a hex number", hex),
            span, "");
        return make_discard(span);
    }

    if (ctx.plugin_runtime_index == nullptr) {
        ctx.diags.error("reader-form-no-data",
            "#form requires --data-dir: no plugin runtime index available",
            span, "");
        return make_discard(span);
    }

    std::string const key = to_lower(plugin);
    auto it = ctx.plugin_runtime_index->find(key);
    if (it == ctx.plugin_runtime_index->end()) {
        ctx.diags.error("reader-form-missing-plugin",
            fmt::format("#form: plugin \"{}\" is not in the load order", plugin),
            span, "");
        return make_discard(span);
    }

    uint32_t const global = mora::ext::globalize_formid(it->second, local);
    return make_form_id_literal(global, span);
}

} // namespace mora_skyrim_compile
