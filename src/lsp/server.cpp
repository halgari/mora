#ifdef _WIN32
// mora lsp is not yet supported on Windows.
// The poll-based run loop uses POSIX select(2) which is not available on
// Windows without additional porting. The LSP server is intended for
// Linux/macOS only until a Windows stdin-polling mechanism is added.
#include "mora/lsp/lsp.h"
#include <iostream>
namespace mora::lsp {
int run(int, char**) {
    std::cerr <<
        "mora lsp is not supported on Windows.\n"
        "  The server's run loop uses POSIX select(2) for stdin polling;\n"
        "  there is no equivalent primitive on Win32 without a porting\n"
        "  layer. Use WSL2 or a native Linux/macOS shell. (The compiler\n"
        "  itself — mora check / compile / inspect — cross-compiles to\n"
        "  Windows via clang-cl + xwin and runs natively.)\n";
    return 1;
}
} // namespace mora::lsp
#else
// POSIX-only implementation below.

#include "mora/lsp/lsp.h"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <sys/select.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "mora/lsp/diagnostics_convert.h"
#include "mora/lsp/dispatch.h"
#include "mora/lsp/document.h"
#include "mora/lsp/framing.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {

void register_lifecycle_handlers(Dispatcher&);
void register_textsync_handlers(Dispatcher&);
void register_hover_handler(Dispatcher&);
void register_definition_handler(Dispatcher&);
void register_references_handler(Dispatcher&);
void register_document_symbols_handler(Dispatcher&);
void register_workspace_symbols_handler(Dispatcher&);
void register_semantic_tokens_handler(Dispatcher&);

namespace {

// Open `path` as the LSP log sink. If `path` is empty, returns a dummy
// stream that discards everything. Returned stream is owned by the caller.
std::unique_ptr<std::ostream> open_log(std::string_view path) {
    if (path.empty()) {
        return std::make_unique<std::ofstream>("/dev/null");
    }
    auto f = std::make_unique<std::ofstream>(std::string(path), std::ios::app);
    return f;
}

void log_event(std::ostream& log, std::string_view event, std::string_view detail = "") {
    log << "[lsp] " << event;
    if (!detail.empty()) log << " " << detail;
    log << "\n";
    log.flush();
}

// Returns true if data is available on stdin within `timeout_ms` ms.
// Uses POSIX select(2) for non-blocking poll without threads.
bool stdin_has_data(int timeout_ms) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
    return r > 0 && FD_ISSET(STDIN_FILENO, &rfds);
}

} // namespace

int run(int argc, char** argv) {
    std::string log_path;
    for (int i = 0; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--version") {
            std::cout << "mora-lsp 0.3.0 (LSP 3.17)\n";
            return 0;
        }
        if (a == "--log" && i + 1 < argc) {
            log_path = argv[++i];
        }
    }

    auto log = open_log(log_path);
    log_event(*log, "starting");

    Workspace ws;
    Dispatcher dispatcher;
    register_lifecycle_handlers(dispatcher);
    register_textsync_handlers(dispatcher);
    register_hover_handler(dispatcher);
    register_definition_handler(dispatcher);
    register_references_handler(dispatcher);
    register_document_symbols_handler(dispatcher);
    register_workspace_symbols_handler(dispatcher);
    register_semantic_tokens_handler(dispatcher);

    while (true) {
        auto now = std::chrono::steady_clock::now();

        // 1. Process debounced reparses that are due.
        for (Document* doc : ws.documents_due_for_reparse(now)) {
            // diagnostics() runs the parse pipeline and clears the stale flag.
            const auto& diags = doc->diagnostics();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& d : diags) arr.push_back(diagnostic_to_json(d));
            ws.enqueue_notification({
                {"jsonrpc", "2.0"},
                {"method", "textDocument/publishDiagnostics"},
                {"params", {
                    {"uri", doc->uri()},
                    {"diagnostics", arr},
                }},
            });
        }

        // 2. Drain any outgoing notifications.
        for (auto& note : ws.drain_outgoing()) {
            write_message(std::cout, note.dump());
        }

        // 3. Poll stdin with a short timeout to support debouncing without threads.
        if (!stdin_has_data(50)) continue;

        // 4. Read and dispatch a message.
        std::string body;
        ReadResult r = read_message(std::cin, body);
        if (r == ReadResult::Eof) {
            log_event(*log, "eof — clean shutdown");
            break;
        }
        if (r == ReadResult::ProtocolError) {
            log_event(*log, "protocol error in headers — aborting");
            return 1;
        }

        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(body);
        } catch (const std::exception& e) {
            log_event(*log, "json parse error", e.what());
            // Per LSP, malformed JSON → reply with a parse error if we have
            // any id we can pull, otherwise drop. We can't extract an id
            // from a malformed body, so just drop.
            continue;
        }

        std::optional<nlohmann::json> reply = dispatcher.dispatch(ws, msg);
        if (reply) {
            write_message(std::cout, reply->dump());
            log_event(*log, "<- response");
        }

        // 5. Drain any server-pushed notifications enqueued by the handler.
        //    (Deferred diagnostics will be drained at the top of next iteration,
        //    but immediate ones — e.g. from didOpen — are drained here too.)
        for (auto& note : ws.drain_outgoing()) {
            write_message(std::cout, note.dump());
        }

        // 6. Check for shutdown-after-exit.
        if (ws.shutdown_requested() && msg.value("method", "") == "exit") {
            log_event(*log, "exit after shutdown — clean");
            return 0;
        }
    }

    return 0;
}

} // namespace mora::lsp
#endif // !_WIN32
