// Portable LSP server loop.
//
// Design: a dedicated reader thread blocks on stdin reading Content-Length
// framed messages, pushing each body onto a queue. The main thread pops
// messages with a 50ms timeout, which lets it interleave debounced
// diagnostic reparses between incoming requests. This replaces an earlier
// POSIX-select(2) polling loop that couldn't run on Windows (select only
// works on sockets there, not pipe/console handles). Same code path on
// every platform now — no #ifdef split.
//
// Threading rules:
//   * std::cin is read ONLY by the reader thread.
//   * std::cout is written ONLY by the main thread.
//   * All cross-thread communication goes through MessageQueue.

#include "mora/lsp/lsp.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

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

// Close status pushed by the reader thread when stdin ends.
enum class CloseReason { None, Eof, ProtocolError };

class MessageQueue {
public:
    void push(std::string msg) {
        {
            std::lock_guard<std::mutex> const lock(mu_);
            messages_.push_back(std::move(msg));
        }
        cv_.notify_one();
    }

    void close(CloseReason reason) {
        {
            std::lock_guard<std::mutex> const lock(mu_);
            close_reason_ = reason;
        }
        cv_.notify_all();
    }

    // Pop the next message, waiting up to `timeout`. Returns nullopt on
    // timeout OR when the queue is closed and empty.
    std::optional<std::string> pop_wait_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, timeout, [this] {
            return !messages_.empty() || close_reason_ != CloseReason::None;
        });
        if (messages_.empty()) return std::nullopt;
        auto msg = std::move(messages_.front());
        messages_.pop_front();
        return msg;
    }

    CloseReason close_reason() {
        std::lock_guard<std::mutex> const lock(mu_);
        return close_reason_;
    }

    bool drained_and_closed() {
        std::lock_guard<std::mutex> const lock(mu_);
        return messages_.empty() && close_reason_ != CloseReason::None;
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> messages_;
    CloseReason close_reason_ = CloseReason::None;
};

void reader_thread_main(MessageQueue& queue) {
    while (true) {
        std::string body;
        ReadResult const r = read_message(std::cin, body);
        if (r == ReadResult::Eof) {
            queue.close(CloseReason::Eof);
            return;
        }
        if (r == ReadResult::ProtocolError) {
            queue.close(CloseReason::ProtocolError);
            return;
        }
        queue.push(std::move(body));
    }
}

} // namespace

int run(int argc, char** argv) {
    std::string log_path;
    for (int i = 0; i < argc; ++i) {
        std::string_view const a = argv[i];
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

    MessageQueue queue;
    std::thread reader([&queue] { reader_thread_main(queue); });

    auto join_reader = [&] {
        if (reader.joinable()) reader.join();
    };

    int exit_code = 0;

    while (true) {
        auto now = std::chrono::steady_clock::now();

        // 1. Process debounced reparses that are due.
        for (Document* doc : ws.documents_due_for_reparse(now)) {
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

        // 3. Wait for the next message with a 50ms debounce timeout.
        auto body_opt = queue.pop_wait_for(std::chrono::milliseconds(50));
        if (!body_opt) {
            // Either a timeout (loop back for diagnostic work) or the
            // reader thread has finished draining stdin.
            if (queue.drained_and_closed()) {
                CloseReason const why = queue.close_reason();
                if (why == CloseReason::ProtocolError) {
                    log_event(*log, "protocol error in headers — aborting");
                    exit_code = 1;
                } else {
                    log_event(*log, "eof — clean shutdown");
                }
                break;
            }
            continue;
        }

        // 4. Parse and dispatch the message.
        nlohmann::json msg;
        try {
            msg = nlohmann::json::parse(*body_opt);
        } catch (const std::exception& e) {
            log_event(*log, "json parse error", e.what());
            continue;
        }

        std::optional<nlohmann::json> reply = dispatcher.dispatch(ws, msg);
        if (reply) {
            write_message(std::cout, reply->dump());
            log_event(*log, "<- response");
        }

        // 5. Drain server-pushed notifications enqueued by the handler
        //    (e.g. immediate diagnostics from didOpen).
        for (auto& note : ws.drain_outgoing()) {
            write_message(std::cout, note.dump());
        }

        // 6. Check for shutdown-after-exit.
        if (ws.shutdown_requested() && msg.value("method", "") == "exit") {
            log_event(*log, "exit after shutdown — clean");
            // Reader may still be blocked on std::cin if the client
            // hasn't closed its write end. Detach so the process can
            // return without the reader forcing a join.
            reader.detach();
            return 0;
        }
    }

    // Clean EOF / protocol-error path: reader thread has already
    // returned, so join is immediate.
    join_reader();
    return exit_code;
}

} // namespace mora::lsp
