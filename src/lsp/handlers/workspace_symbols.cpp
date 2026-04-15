#include <algorithm>
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

bool fuzzy_contains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(), needle.end(),
                          [](unsigned char a, unsigned char b) {
                              return std::tolower(a) == std::tolower(b);
                          });
    return it != haystack.end();
}

Result on_workspace_symbol(Workspace& ws, const nlohmann::json& params) {
    std::string query = params.at("query").get<std::string>();
    nlohmann::json results = nlohmann::json::array();
    for (Document* doc : ws.all_documents()) {
        // Ensure parse is up to date before reading the symbol index.
        (void)doc->diagnostics();

        const auto& pool = doc->pool();
        // Find this doc's namespace name (if any).
        std::string ns_name;
        for (const auto& e : doc->symbol_index().entries()) {
            if (e.kind == SymbolKind::Namespace) {
                ns_name = std::string(pool.get(e.name));
                break;
            }
        }
        for (const auto& e : doc->symbol_index().entries()) {
            if (e.kind != SymbolKind::RuleHead) continue;
            std::string rule_name = std::string(pool.get(e.name));
            std::string qualified = ns_name.empty() ? rule_name : (ns_name + "." + rule_name);
            if (!fuzzy_contains(qualified, query)) continue;
            results.push_back({
                {"name", qualified},
                {"kind", 12},  // Function
                {"location", {
                    {"uri", doc->uri()},
                    {"range", span_to_range(e.span)},
                }},
            });
        }
    }
    return results;
}

} // namespace

void register_workspace_symbols_handler(Dispatcher& d) {
    d.on_request("workspace/symbol", on_workspace_symbol);
}

} // namespace mora::lsp
