#pragma once

// Forward declarations for per-handler registration functions. Each
// src/lsp/handlers/<area>.cpp defines exactly one register_*_handler
// free function; both src/lsp/server.cpp (to wire every handler into
// a Dispatcher) and the per-handler unit tests include this header.
//
// Keeping these in a shared header — rather than forward-declaring in
// each caller — makes the linkage explicit, avoids drift if a
// signature changes, and silences misc-use-internal-linkage.

namespace mora::lsp {

class Dispatcher;

void register_lifecycle_handlers(Dispatcher& d);
void register_textsync_handlers(Dispatcher& d);
void register_hover_handler(Dispatcher& d);
void register_definition_handler(Dispatcher& d);
void register_references_handler(Dispatcher& d);
void register_document_symbols_handler(Dispatcher& d);
void register_workspace_symbols_handler(Dispatcher& d);
void register_semantic_tokens_handler(Dispatcher& d);

} // namespace mora::lsp
