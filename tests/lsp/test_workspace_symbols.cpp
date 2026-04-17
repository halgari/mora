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

TEST(LspWorkspaceSymbols, MatchesAcrossDocuments) {
    Workspace ws;
    Dispatcher d; register_workspace_symbols_handler(d);
    ws.open("file:///a.mora",
        "namespace test.a\n"
        "bandit(NPC):\n    form/npc(NPC)\n", 1);
    ws.open("file:///b.mora",
        "namespace test.b\n"
        "iron_weapons(W):\n    form/weapon(W)\n", 1);

    auto reply = d.dispatch(ws, req(1, "workspace/symbol", {
        {"query", "bandit"},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]["name"], "test.a.bandit");
    EXPECT_EQ(result[0]["kind"], 12);
    EXPECT_EQ(result[0]["location"]["uri"], "file:///a.mora");
}

TEST(LspWorkspaceSymbols, EmptyQueryReturnsAllRules) {
    Workspace ws;
    Dispatcher d; register_workspace_symbols_handler(d);
    ws.open("file:///a.mora",
        "namespace test.a\nbandit(NPC):\n    form/npc(NPC)\n", 1);

    auto reply = d.dispatch(ws, req(1, "workspace/symbol", {{"query", ""}}));
    auto& result = (*reply)["result"];
    EXPECT_GE(result.size(), 1u);
}
