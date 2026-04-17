#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/handlers.h"
#include "mora/lsp/workspace.h"

using nlohmann::json;
using namespace mora::lsp;

namespace {
json req(int id, std::string_view method, json params = {}) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
json note(std::string_view method, json params = {}) {
    return {{"jsonrpc","2.0"},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspLifecycle, InitializeReturnsCapabilities) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);

    auto r = d.dispatch(ws, req(1, "initialize", json::object()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)["id"], 1);
    ASSERT_TRUE(r->contains("result"));
    auto caps = (*r)["result"]["capabilities"];
    EXPECT_TRUE(caps.contains("textDocumentSync"));
}

TEST(LspLifecycle, InitializedNotificationMarksReady) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);

    d.dispatch(ws, req(1, "initialize"));
    EXPECT_FALSE(ws.initialized());

    auto r = d.dispatch(ws, note("initialized"));
    EXPECT_FALSE(r.has_value());  // notifications don't reply
    EXPECT_TRUE(ws.initialized());
}

TEST(LspLifecycle, ShutdownReturnsNullResult) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);
    d.dispatch(ws, req(1, "initialize"));
    d.dispatch(ws, note("initialized"));

    auto r = d.dispatch(ws, req(2, "shutdown"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ((*r)["id"], 2);
    EXPECT_TRUE((*r)["result"].is_null());
    EXPECT_TRUE(ws.shutdown_requested());
}

TEST(LspLifecycle, RequestBeforeInitializeReturnsServerNotInitialized) {
    Workspace ws;
    Dispatcher d;
    register_lifecycle_handlers(d);

    // Pretend a textDocument/foo request arrived before initialize.
    // We have no such handler yet, but a request to "shutdown" before
    // initialize is the canonical test:
    auto r = d.dispatch(ws, req(2, "shutdown"));
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->contains("error"));
    EXPECT_EQ((*r)["error"]["code"], static_cast<int>(ErrorCode::ServerNotInitialized));
}
