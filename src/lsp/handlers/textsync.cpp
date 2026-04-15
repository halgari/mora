#include <chrono>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/document.h"
#include "mora/lsp/diagnostics_convert.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

namespace {

void publish_diagnostics(Workspace& ws, std::string_view uri) {
    Document* doc = ws.get(uri);
    if (!doc) return;
    const auto& diags = doc->diagnostics();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : diags) arr.push_back(diagnostic_to_json(d));
    ws.enqueue_notification({
        {"jsonrpc", "2.0"},
        {"method",  "textDocument/publishDiagnostics"},
        {"params",  {
            {"uri",         std::string(uri)},
            {"diagnostics", arr},
        }},
    });
}

Result on_did_open(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    std::string uri = td.at("uri").get<std::string>();
    ws.open(uri,
            td.at("text").get<std::string>(),
            td.at("version").get<int>());
    // Initial open publishes diagnostics immediately — the user just opened
    // the file and expects squigglies fast. No debouncing here.
    publish_diagnostics(ws, uri);
    return nlohmann::json{};
}

Result on_did_change(Workspace& ws, const nlohmann::json& params) {
    const auto& td = params.at("textDocument");
    const auto& changes = params.at("contentChanges");
    if (changes.empty()) return nlohmann::json{};
    // textDocumentSync = Full: exactly one entry whose `text` is the entire
    // new content.
    const std::string text = changes.back().at("text").get<std::string>();
    std::string uri = td.at("uri").get<std::string>();
    ws.change(uri, text, td.at("version").get<int>());
    // Schedule a reparse 150ms in the future. The poll-based run loop calls
    // documents_due_for_reparse() and publishes diagnostics when due.
    // This coalesces rapid keystrokes into a single reparse.
    auto* doc = ws.get(uri);
    if (doc) {
        doc->schedule_reparse(
            std::chrono::steady_clock::now() + std::chrono::milliseconds(150));
    }
    return nlohmann::json{};
}

Result on_did_close(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    ws.close(uri);
    // Clear stale diagnostics on the client side.
    ws.enqueue_notification({
        {"jsonrpc", "2.0"},
        {"method",  "textDocument/publishDiagnostics"},
        {"params",  {{"uri", uri}, {"diagnostics", nlohmann::json::array()}}},
    });
    return nlohmann::json{};
}

Result on_did_save(Workspace&, const nlohmann::json&) {
    // No-op for v1 — diagnostics already published on didChange (debounced).
    return nlohmann::json{};
}

} // namespace

void register_textsync_handlers(Dispatcher& d) {
    d.on_notification("textDocument/didOpen",   on_did_open);
    d.on_notification("textDocument/didChange", on_did_change);
    d.on_notification("textDocument/didClose",  on_did_close);
    d.on_notification("textDocument/didSave",   on_did_save);
}

} // namespace mora::lsp
