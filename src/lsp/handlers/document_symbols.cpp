#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

nlohmann::json span_to_range(const mora::SourceSpan& span) {
    auto zb = [](uint32_t one) -> int { return one == 0 ? 0 : static_cast<int>(one - 1); };
    return {
        {"start", {{"line", zb(span.start_line)}, {"character", zb(span.start_col)}}},
        {"end",   {{"line", zb(span.end_line)},   {"character", zb(span.end_col)}}},
    };
}

Result on_document_symbol(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json::array();

    // Ensure parse is up to date before reading the symbol index.
    (void)doc->diagnostics();

    nlohmann::json result = nlohmann::json::array();

    // Top-level: a single Namespace symbol containing rule children.
    nlohmann::json children = nlohmann::json::array();
    nlohmann::json ns_entry = nullptr;
    const auto& pool = doc->pool();

    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == SymbolKind::Namespace) {
            ns_entry = {
                {"name", std::string(pool.get(e.name))},
                {"kind", 3},  // SymbolKind.Namespace per LSP
                {"range", span_to_range(e.span)},
                {"selectionRange", span_to_range(e.span)},
            };
        } else if (e.kind == SymbolKind::RuleHead) {
            children.push_back({
                {"name", std::string(pool.get(e.name))},
                {"kind", 12},  // SymbolKind.Function
                {"range", span_to_range(e.span)},
                {"selectionRange", span_to_range(e.span)},
            });
        }
    }

    if (!ns_entry.is_null()) {
        ns_entry["children"] = children;
        result.push_back(ns_entry);
    } else {
        // No namespace: emit rules as top-level.
        for (auto& c : children) result.push_back(c);
    }

    return result;
}

} // namespace

void register_document_symbols_handler(Dispatcher& d) {
    d.on_request("textDocument/documentSymbol", on_document_symbol);
}

} // namespace mora::lsp
