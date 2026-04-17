#include <filesystem>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/uri.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

namespace {

nlohmann::json server_capabilities() {
    return {
        // textDocumentSync = 1 (full) for v1; can move to incremental in
        // a follow-up. The full-sync mode receives the entire document
        // text on every change, which is fine at our document sizes.
        {"textDocumentSync", 1},
        // Task 6: advertise hover support so VS Code will call textDocument/hover.
        {"hoverProvider", true},
        // Task 7: goto-definition.
        {"definitionProvider", true},
        // Task 8: find-all-references.
        {"referencesProvider", true},
        // Task 9: document outline.
        {"documentSymbolProvider", true},
        // Task 10: workspace symbol search.
        {"workspaceSymbolProvider", true},
        // Task 11: semantic tokens.
        // Legend order MUST match the TT_* constants in semantic_tokens.cpp.
        {"semanticTokensProvider", {
            {"legend", {
                {"tokenTypes", {
                    "function", "function", "parameter", "parameter",
                    "enumMember", "enumMember", "namespace",
                }},
                {"tokenModifiers", {"defaultLibrary", "deprecated"}},
            }},
            {"full", true},
        }},
    };
}

Result on_initialize(Workspace& ws, const nlohmann::json& params) {
    // Default: <rootUri>/data/relations/  (matches the project convention)
    std::filesystem::path relations_dir;
    if (params.contains("rootUri") && params["rootUri"].is_string()) {
        std::string const root_uri = params["rootUri"].get<std::string>();
        std::string const root_path = path_from_uri(root_uri);
        if (!root_path.empty()) {
            relations_dir = std::filesystem::path(root_path) / "data" / "relations";
        }
    }
    // initializationOptions.relationsDir and .dataDir override if provided
    if (params.contains("initializationOptions")) {
        const auto& opts = params["initializationOptions"];
        if (opts.contains("relationsDir") && opts["relationsDir"].is_string()) {
            relations_dir = opts["relationsDir"].get<std::string>();
        }
        if (opts.contains("dataDir") && opts["dataDir"].is_string()) {
            ws.set_data_dir(opts["dataDir"].get<std::string>());
        }
    }
    ws.set_relations_dir(relations_dir);

    return nlohmann::json{
        {"capabilities", server_capabilities()},
        {"serverInfo", {{"name", "mora-lsp"}, {"version", "0.3.0"}}},
    };
}

Result on_initialized(Workspace& ws, const nlohmann::json&) {
    ws.mark_initialized();
    return nlohmann::json{};  // unused for notifications
}

Result on_shutdown(Workspace& ws, const nlohmann::json&) {
    if (!ws.initialized()) {
        return Error{ErrorCode::ServerNotInitialized, "shutdown before initialize"};
    }
    ws.request_shutdown();
    return nlohmann::json();  // null result
}

Result on_exit(Workspace&, const nlohmann::json&) {
    // The run loop interprets exit by checking shutdown_requested before
    // tearing down. This handler is a no-op aside from acknowledging
    // receipt; the loop check happens after dispatch.
    return nlohmann::json{};
}

} // namespace

void register_lifecycle_handlers(Dispatcher& d) {
    d.on_request     ("initialize",  on_initialize);
    d.on_notification("initialized", on_initialized);
    d.on_request     ("shutdown",    on_shutdown);
    d.on_notification("exit",        on_exit);
}

} // namespace mora::lsp
