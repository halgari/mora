#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"

namespace mora::lsp {
void register_document_symbols_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspDocumentSymbols, ListsNamespaceAndRules) {
    Workspace ws;
    Dispatcher d; register_document_symbols_handler(d);
    ws.open("file:///x.mora",
        "namespace test.example\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n"
        "iron_weapons(W):\n"
        "    form/weapon(W)\n", 1);

    auto reply = d.dispatch(ws, req(1, "textDocument/documentSymbol", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1u);  // 1 top-level Namespace
    EXPECT_EQ(result[0]["name"], "test.example");
    EXPECT_EQ(result[0]["kind"], 3);  // Namespace
    auto& children = result[0]["children"];
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0]["name"], "bandit");
    EXPECT_EQ(children[0]["kind"], 12);  // Function
    EXPECT_EQ(children[1]["name"], "iron_weapons");
}
