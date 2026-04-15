#include <algorithm>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

// Token type indices — must match the legend order in server_capabilities().
// The *_DEP variants and TM_DEPRECATED are reserved for a follow-up that
// tags undefined/unresolved tokens; v1's SymbolIndex doesn't carry that
// info yet, so they're declared (so the legend stays stable) but unused.
constexpr uint32_t TT_FUNCTION        = 0;  // defined rule head / call
[[maybe_unused]] constexpr uint32_t TT_FUNCTION_DEP    = 1;
constexpr uint32_t TT_PARAMETER       = 2;  // variable binding or use (bound)
[[maybe_unused]] constexpr uint32_t TT_PARAMETER_DEP   = 3;
constexpr uint32_t TT_ENUM_MEMBER     = 4;  // resolved atom
[[maybe_unused]] constexpr uint32_t TT_ENUM_MEMBER_DEP = 5;
constexpr uint32_t TT_NAMESPACE       = 6;  // built-in namespace

// Modifier bit flags — must match tokenModifiers legend.
constexpr uint32_t TM_DEFAULT_LIB = 1u << 0;  // built-in relation
[[maybe_unused]] constexpr uint32_t TM_DEPRECATED = 1u << 1;

Result on_semantic_tokens_full(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    Document* doc = ws.get(uri);
    nlohmann::json data = nlohmann::json::array();
    if (!doc) return nlohmann::json{{"data", data}};

    // Ensure parse is up to date before reading the symbol index.
    (void)doc->diagnostics();

    // Copy entries and sort by (start_line, start_col) for delta encoding.
    // The LSP wire format requires tokens to be ordered by position.
    auto entries = doc->symbol_index().entries();
    std::sort(entries.begin(), entries.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) {
                  if (a.span.start_line != b.span.start_line)
                      return a.span.start_line < b.span.start_line;
                  return a.span.start_col < b.span.start_col;
              });

    uint32_t prev_line = 0, prev_col = 0;
    for (const auto& e : entries) {
        // Skip multi-line tokens — the LSP wire format only supports
        // single-line tokens in the standard encoding.
        if (e.span.start_line != e.span.end_line) continue;
        // Skip zero-length tokens (degenerate spans).
        if (e.span.start_col == 0) continue;

        uint32_t type      = TT_FUNCTION;
        uint32_t modifiers = 0;
        switch (e.kind) {
            case SymbolKind::RuleHead:
            case SymbolKind::RuleCall:
                type = TT_FUNCTION;
                break;
            case SymbolKind::Relation:
                type = TT_FUNCTION;
                modifiers |= TM_DEFAULT_LIB;
                break;
            case SymbolKind::VariableBinding:
            case SymbolKind::VariableUse:
                type = TT_PARAMETER;
                break;
            case SymbolKind::Atom:
                type = TT_ENUM_MEMBER;
                break;
            case SymbolKind::Namespace:
                type = TT_NAMESPACE;
                break;
        }

        // Convert 1-based mora coords to 0-based LSP coords.
        uint32_t line = e.span.start_line - 1;
        uint32_t col  = e.span.start_col  - 1;
        // Token length: end_col - start_col (both 1-based, same line).
        uint32_t len  = (e.span.end_col > e.span.start_col)
                        ? (e.span.end_col - e.span.start_col)
                        : 1;

        // Delta encoding: deltaLine is absolute from previous token's line.
        // deltaStart is absolute from previous token's start col if same line,
        // else absolute from 0.
        uint32_t dl = line - prev_line;
        uint32_t dc = (dl == 0) ? (col - prev_col) : col;

        data.push_back(dl);
        data.push_back(dc);
        data.push_back(len);
        data.push_back(type);
        data.push_back(modifiers);

        prev_line = line;
        prev_col  = col;
    }

    return nlohmann::json{{"data", data}};
}

} // namespace

void register_semantic_tokens_handler(Dispatcher& d) {
    d.on_request("textDocument/semanticTokens/full", on_semantic_tokens_full);
}

} // namespace mora::lsp
