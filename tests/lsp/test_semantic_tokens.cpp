#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/handlers.h"
#include "mora/lsp/workspace.h"

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspSemanticTokens, EmitsTokensForRulesAndVariables) {
    Workspace ws;
    Dispatcher d; register_semantic_tokens_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/semanticTokens/full", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.contains("data"));
    auto& data = result["data"];
    ASSERT_TRUE(data.is_array());
    // Each token = 5 ints. With at least: namespace, bandit, NPC (binding),
    // form/npc (relation), NPC (use) — at least 5 tokens.
    EXPECT_EQ(data.size() % 5, 0u);
    EXPECT_GE(data.size(), 5u * 5u);  // at least 5 tokens
}

TEST(LspSemanticTokens, EmptyDocumentReturnsEmptyData) {
    Workspace ws;
    Dispatcher d; register_semantic_tokens_handler(d);
    ws.open("file:///x.mora", "", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/semanticTokens/full", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& data = (*reply)["result"]["data"];
    EXPECT_TRUE(data.is_array());
    EXPECT_EQ(data.size(), 0u);
}
