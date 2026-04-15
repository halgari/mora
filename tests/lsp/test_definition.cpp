#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {
void register_definition_handler(Dispatcher&);
}

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
json position_params(std::string_view uri, int line, int character) {
    return {
        {"textDocument", {{"uri", std::string(uri)}}},
        {"position",     {{"line", line}, {"character", character}}},
    };
}
} // namespace

// Helper: open the document, trigger parse, and find the first entry of a given kind.
static const SymbolEntry* find_entry(Document* doc, SymbolKind kind) {
    (void)doc->diagnostics();
    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == kind) return &e;
    }
    return nullptr;
}

TEST(LspDefinition, VariableUseJumpsToBinding) {
    Workspace ws;
    Dispatcher d; register_definition_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"          // line 1 (0-based) = line 2 (1-based); NPC binds at head
        "    form/npc(NPC)\n",    // line 2 (0-based); NPC use
        1);

    // Find the VariableUse entry to get its exact position, then issue goto-def.
    Document* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    (void)doc->diagnostics();

    const SymbolEntry* use_entry = nullptr;
    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == SymbolKind::VariableUse) { use_entry = &e; break; }
    }
    ASSERT_NE(use_entry, nullptr) << "Expected a VariableUse entry";

    int line_zb = static_cast<int>(use_entry->span.start_line) - 1;
    int col_zb  = static_cast<int>(use_entry->span.start_col)  - 1;

    auto reply = d.dispatch(ws, req(1, "textDocument/definition",
        position_params("file:///x.mora", line_zb, col_zb)));
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("result"));
    auto& result = (*reply)["result"];
    ASSERT_FALSE(result.is_null()) << "Expected a location, got null";

    // The binding is on line 1 (0-based), which is the rule head line.
    if (result.is_array()) {
        ASSERT_FALSE(result.empty());
        EXPECT_EQ(result[0]["range"]["start"]["line"], 1);
    } else {
        EXPECT_EQ(result["range"]["start"]["line"], 1);
    }
}

TEST(LspDefinition, VariableBindingJumpsToItself) {
    Workspace ws;
    Dispatcher d; register_definition_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n",
        1);

    Document* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    (void)doc->diagnostics();

    const SymbolEntry* bind_entry = find_entry(doc, SymbolKind::VariableBinding);
    ASSERT_NE(bind_entry, nullptr);

    int line_zb = static_cast<int>(bind_entry->span.start_line) - 1;
    int col_zb  = static_cast<int>(bind_entry->span.start_col)  - 1;

    auto reply = d.dispatch(ws, req(2, "textDocument/definition",
        position_params("file:///x.mora", line_zb, col_zb)));
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("result"));
    auto& result = (*reply)["result"];
    ASSERT_FALSE(result.is_null());

    // Location should be on line 1 (0-based), same as binding.
    auto& loc = result.is_array() ? result[0] : result;
    EXPECT_EQ(loc["range"]["start"]["line"], line_zb);
}

TEST(LspDefinition, RuleCallJumpsToRuleHead) {
    Workspace ws;
    Dispatcher d; register_definition_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "armed(NPC):\n"           // line 1 (0-based) — definition
        "    form/weapon(NPC)\n"
        "dangerous(NPC):\n"       // line 3 (0-based)
        "    armed(NPC)\n",       // line 4 (0-based) — RuleCall to armed
        1);

    Document* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    (void)doc->diagnostics();

    // Find the RuleCall entry.
    const SymbolEntry* call_entry = nullptr;
    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == SymbolKind::RuleCall) { call_entry = &e; break; }
    }
    ASSERT_NE(call_entry, nullptr) << "Expected a RuleCall entry for armed";

    int line_zb = static_cast<int>(call_entry->span.start_line) - 1;
    int col_zb  = static_cast<int>(call_entry->span.start_col)  - 1;

    auto reply = d.dispatch(ws, req(3, "textDocument/definition",
        position_params("file:///x.mora", line_zb, col_zb)));
    ASSERT_TRUE(reply.has_value());
    ASSERT_TRUE(reply->contains("result"));
    auto& result = (*reply)["result"];
    ASSERT_FALSE(result.is_null()) << "Expected a location for armed rule";

    auto& loc = result.is_array() ? result[0] : result;
    // armed(NPC): is on line 1 (0-based)
    EXPECT_EQ(loc["range"]["start"]["line"], 1);
}

TEST(LspDefinition, NoMatchReturnsNull) {
    Workspace ws;
    Dispatcher d; register_definition_handler(d);
    ws.open("file:///x.mora", "namespace t\n", 1);
    auto reply = d.dispatch(ws, req(4, "textDocument/definition",
        position_params("file:///x.mora", 99, 99)));
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE((*reply)["result"].is_null());
}
