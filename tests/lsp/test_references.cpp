#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "mora/lsp/dispatch.h"
#include "mora/lsp/document.h"
#include "mora/lsp/handlers.h"
#include "mora/lsp/symbol_index.h"
#include "mora/lsp/workspace.h"

using namespace mora::lsp;
using nlohmann::json;

namespace {
json req(int id, std::string_view method, json params) {
    return {{"jsonrpc","2.0"},{"id",id},{"method",std::string(method)},{"params",params}};
}
} // namespace

TEST(LspReferences, FindsAllVariableOccurrencesInRule) {
    Workspace ws;
    Dispatcher d; register_references_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n"
        "    form/level(NPC, _)\n", 1);

    // Force parse and find the NPC binding entry to get its exact column.
    Document* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    (void)doc->diagnostics();

    const SymbolEntry* bind_entry = nullptr;
    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == SymbolKind::VariableBinding) { bind_entry = &e; break; }
    }
    ASSERT_NE(bind_entry, nullptr) << "Expected a VariableBinding entry for NPC";

    int line_zb = static_cast<int>(bind_entry->span.start_line) - 1;
    int col_zb  = static_cast<int>(bind_entry->span.start_col)  - 1;

    auto reply = d.dispatch(ws, req(1, "textDocument/references", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
        {"position",     {{"line", line_zb}, {"character", col_zb}}},
        {"context",      {{"includeDeclaration", true}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    // NPC appears: binding in head + use in form/npc + use in form/level = 3.
    EXPECT_EQ(result.size(), 3u);
}

TEST(LspReferences, ExcludeDeclarationOmitsBinding) {
    Workspace ws;
    Dispatcher d; register_references_handler(d);
    ws.open("file:///x.mora",
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n"
        "    form/level(NPC, _)\n", 1);

    Document* doc = ws.get("file:///x.mora");
    ASSERT_NE(doc, nullptr);
    (void)doc->diagnostics();

    const SymbolEntry* bind_entry = nullptr;
    for (const auto& e : doc->symbol_index().entries()) {
        if (e.kind == SymbolKind::VariableBinding) { bind_entry = &e; break; }
    }
    ASSERT_NE(bind_entry, nullptr);

    int line_zb = static_cast<int>(bind_entry->span.start_line) - 1;
    int col_zb  = static_cast<int>(bind_entry->span.start_col)  - 1;

    auto reply = d.dispatch(ws, req(2, "textDocument/references", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
        {"position",     {{"line", line_zb}, {"character", col_zb}}},
        {"context",      {{"includeDeclaration", false}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    // Without the declaration: only the 2 uses remain.
    EXPECT_EQ(result.size(), 2u);
}

TEST(LspReferences, RuleReferencesAcrossDocuments) {
    Workspace ws;
    Dispatcher d; register_references_handler(d);
    ws.open("file:///a.mora",
        "namespace t\n"
        "armed(NPC):\n"          // definition
        "    form/weapon(NPC)\n", 1);
    ws.open("file:///b.mora",
        "namespace t\n"
        "dangerous(NPC):\n"
        "    armed(NPC)\n", 1);  // call site

    // Force parse both documents.
    ws.get("file:///a.mora")->diagnostics();
    ws.get("file:///b.mora")->diagnostics();

    // Find the RuleHead in a.mora.
    Document* a = ws.get("file:///a.mora");
    const SymbolEntry* head_entry = nullptr;
    for (const auto& e : a->symbol_index().entries()) {
        if (e.kind == SymbolKind::RuleHead) { head_entry = &e; break; }
    }
    ASSERT_NE(head_entry, nullptr);

    int line_zb = static_cast<int>(head_entry->span.start_line) - 1;
    int col_zb  = static_cast<int>(head_entry->span.start_col)  - 1;

    auto reply = d.dispatch(ws, req(3, "textDocument/references", {
        {"textDocument", {{"uri", "file:///a.mora"}}},
        {"position",     {{"line", line_zb}, {"character", col_zb}}},
        {"context",      {{"includeDeclaration", true}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    // Should find the RuleHead in a.mora + RuleCall in b.mora = 2.
    EXPECT_EQ(result.size(), 2u);
}

TEST(LspReferences, NoMatchReturnsEmptyArray) {
    Workspace ws;
    Dispatcher d; register_references_handler(d);
    ws.open("file:///x.mora", "namespace t\n", 1);
    auto reply = d.dispatch(ws, req(4, "textDocument/references", {
        {"textDocument", {{"uri", "file:///x.mora"}}},
        {"position",     {{"line", 99}, {"character", 99}}},
        {"context",      {{"includeDeclaration", true}}},
    }));
    ASSERT_TRUE(reply.has_value());
    auto& result = (*reply)["result"];
    ASSERT_TRUE(result.is_array());
    EXPECT_TRUE(result.empty());
}
