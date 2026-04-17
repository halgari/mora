#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/document.h"
#include "mora/lsp/handlers.h"
#include "mora/lsp/workspace.h"

using namespace mora::lsp;
using nlohmann::json;

namespace {
json note(std::string_view method, json params = {}) {
    return {{"jsonrpc","2.0"},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspWorkspace, DidOpenCreatesDocument) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    json params = {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\n"},
        }},
    };
    d.dispatch(ws, note("textDocument/didOpen", params));
    ASSERT_EQ(ws.document_count(), 1u);
    auto* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->text(), "namespace t\n");
    EXPECT_EQ(doc->version(), 1);
}

TEST(LspWorkspace, DidChangeUpdatesText) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\n"},
        }},
    }));
    d.dispatch(ws, note("textDocument/didChange", {
        {"textDocument", {{"uri", "file:///x.mora"}, {"version", 2}}},
        {"contentChanges", json::array({json{{"text", "namespace u\n"}}})},
    }));

    auto* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->text(), "namespace u\n");
    EXPECT_EQ(doc->version(), 2);
}

TEST(LspWorkspace, DidCloseRemovesDocument) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", ""},
        }},
    }));
    EXPECT_EQ(ws.document_count(), 1u);

    d.dispatch(ws, note("textDocument/didClose", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    EXPECT_EQ(ws.document_count(), 0u);
}

TEST(LspWorkspace, DidOpenWithMissingFieldReturnsNoCrash) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    // textDocument missing the "text" field — should not crash.
    auto reply = d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            // text intentionally missing
        }},
    }));
    // Notification → no reply expected.
    EXPECT_FALSE(reply.has_value());
    // No document was created (handler errored out before reaching ws.open).
    EXPECT_EQ(ws.document_count(), 0u);
}

TEST(LspWorkspace, DidOpenEnqueuesPublishDiagnostics) {
    Workspace ws;
    Dispatcher d;
    register_textsync_handlers(d);

    d.dispatch(ws, note("textDocument/didOpen", {
        {"textDocument", {
            {"uri", "file:///x.mora"},
            {"languageId", "mora"},
            {"version", 1},
            {"text", "namespace t\n"},
        }},
    }));

    auto outgoing = ws.drain_outgoing();
    ASSERT_EQ(outgoing.size(), 1u);
    EXPECT_EQ(outgoing[0]["method"], "textDocument/publishDiagnostics");
    EXPECT_EQ(outgoing[0]["params"]["uri"], "file:///x.mora");
}

TEST(LspWorkspace, GetByUriAfterOpenReturnsDocument) {
    Workspace ws;
    Document* opened = ws.open("file:///x.mora", "namespace t\n", 1);
    ASSERT_NE(opened, nullptr);
    Document* fetched = ws.get("file:///x.mora");
    EXPECT_EQ(opened, fetched) << "get() by URI must find the same Document open() returned";
}

TEST(LspWorkspace, RequestWithMalformedParamsReturnsInvalidParams) {
    Workspace ws;
    Dispatcher d;
    // Register a handler that throws.
    d.on_request("test/throw", [](Workspace&, const nlohmann::json& p) -> Result {
        return p.at("required_field");  // throws if missing
    });

    nlohmann::json msg = {
        {"jsonrpc","2.0"},
        {"id",42},
        {"method","test/throw"},
        {"params", nlohmann::json::object()},
    };
    auto reply = d.dispatch(ws, msg);
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("error"));
    EXPECT_EQ((*reply)["error"]["code"], static_cast<int>(ErrorCode::InvalidParams));
    EXPECT_EQ((*reply)["id"], 42);
}

TEST(LspWorkspace, SetRelationsDirLoadsDefaultsRegardlessOfPath) {
    Workspace ws;
    // Empty path: defaults still load (compiled-in).
    ws.set_relations_dir(std::filesystem::path{});
    EXPECT_GT(ws.schema().relation_count(), 0u);

    // Non-empty path: same (defaults still load + path stored for goto-def).
    ws.set_relations_dir("/some/path");
    EXPECT_GT(ws.schema().relation_count(), 0u);
}

TEST(LspWorkspace, ReparseDuesAfterDeadline) {
    Workspace ws;
    Document* doc = ws.open("file:///x.mora", "namespace t\n", 1);
    auto now = std::chrono::steady_clock::now();

    // Initial: not due (no reparse scheduled; cache_stale_ is true but
    // reparse_after_ is at the epoch so now >= reparse_after_ immediately).
    // Actually, on initial open cache_stale_=true and reparse_after_={},
    // which means reparse_due() returns true immediately. Call diagnostics
    // first to clear the stale flag before testing schedule_reparse.
    (void)doc->diagnostics();
    EXPECT_TRUE(ws.documents_due_for_reparse(now).empty());

    // Schedule a reparse 50ms in the future.
    doc->schedule_reparse(now + std::chrono::milliseconds(50));
    // Calling update() to mark cache_stale_ again (schedule_reparse alone
    // doesn't set cache_stale_).
    doc->update("namespace t\n", 2);
    EXPECT_TRUE(ws.documents_due_for_reparse(now).empty()) << "not due yet";

    // After deadline: due.
    auto later = now + std::chrono::milliseconds(60);
    auto due = ws.documents_due_for_reparse(later);
    EXPECT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0], doc);

    // After diagnostics() is called (clears stale flag), no longer due.
    (void)doc->diagnostics();
    EXPECT_TRUE(ws.documents_due_for_reparse(later).empty());
}
