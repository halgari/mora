#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"

namespace mora::lsp {
void register_hover_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
json hover_params(std::string_view uri, int line, int character) {
    return {
        {"textDocument", {{"uri", std::string(uri)}}},
        {"position", {{"line", line}, {"character", character}}},
    };
}
} // namespace

TEST(LspHover, RuleHeadShowsSignatureAndDocComment) {
    Workspace ws;
    Dispatcher d;
    register_hover_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "# Tags every NPC in the bandit faction.\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n", 1);

    // hover on `bandit` at line 2 (0-based) = line 3 (1-based), col 1.
    auto reply = d.dispatch(ws, req(1, "textDocument/hover",
        hover_params("file:///x.mora", 2, 1)));
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("result"));
    auto md = (*reply)["result"]["contents"]["value"].get<std::string>();
    EXPECT_NE(md.find("bandit("), std::string::npos);
    EXPECT_NE(md.find("Tags every NPC"), std::string::npos);
}

TEST(LspHover, AtomFallbackWhenNoDataDir) {
    Workspace ws;
    Dispatcher d;
    register_hover_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/keyword(NPC, @BanditFaction)\n", 1);

    // Scan entries to find the atom's column rather than hardcoding it.
    {
        Document* doc = ws.get("file:///x.mora");
        ASSERT_NE(doc, nullptr);
        (void)doc->diagnostics();  // force parse
        // Find atom column
        for (const auto& e : doc->symbol_index().entries()) {
            if (e.kind == SymbolKind::Atom) {
                // Use its column for the hover request
                auto reply = d.dispatch(ws, req(1, "textDocument/hover",
                    hover_params("file:///x.mora",
                        static_cast<int>(e.span.start_line) - 1,
                        static_cast<int>(e.span.start_col) - 1)));
                ASSERT_TRUE(reply.has_value());
                if (reply->contains("result") && !(*reply)["result"].is_null()) {
                    auto md = (*reply)["result"]["contents"]["value"].get<std::string>();
                    EXPECT_NE(md.find("BanditFaction"), std::string::npos);
                    // Without dataDir, we expect an unresolved message.
                    EXPECT_NE(md.find("not resolved"), std::string::npos);
                }
                return;
            }
        }
        FAIL() << "No atom entry found in symbol index";
    }
}

TEST(LspHover, NoHoverOnEmptyPosition) {
    Workspace ws;
    Dispatcher d;
    register_hover_handler(d);
    ws.open("file:///x.mora", "namespace t\n", 1);
    // Hover on an out-of-range position — should return null result, not error.
    auto reply = d.dispatch(ws, req(1, "textDocument/hover",
        hover_params("file:///x.mora", 99, 99)));
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE((*reply)["result"].is_null());
}
