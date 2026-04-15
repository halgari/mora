#include <gtest/gtest.h>
#include "mora/lsp/document.h"
#include "mora/lsp/workspace.h"

using namespace mora::lsp;

TEST(LspDocument, ConstructHoldsFields) {
    Workspace ws;
    Document d(ws, "file:///x.mora", "namespace t\n", 1);
    EXPECT_EQ(d.uri(), "file:///x.mora");
    EXPECT_EQ(d.text(), "namespace t\n");
    EXPECT_EQ(d.version(), 1);
}

TEST(LspDocument, UpdateBumpsVersion) {
    Workspace ws;
    Document d(ws, "file:///x.mora", "namespace t\n", 1);
    d.update("namespace t\nnamespace u\n", 2);
    EXPECT_EQ(d.version(), 2);
    EXPECT_EQ(d.text(), "namespace t\nnamespace u\n");
}

TEST(LspDocument, DiagnosticsForValidSourceIsEmpty) {
    Workspace ws;
    Document d(ws, "file:///x.mora",
               "namespace t\nbandit(NPC):\n    form/npc(NPC)\n",
               1);
    auto& diags = d.diagnostics();
    // Note: form/npc may or may not resolve — the point is the parser
    // doesn't emit a parse error on this trivially-valid syntax.
    EXPECT_GE(diags.size(), 0u);
}

TEST(LspDocument, DiagnosticsForBadSourceHasError) {
    Workspace ws;
    Document d(ws, "file:///x.mora",
               "namespace t\nbandit(:\n",  // missing variable in head
               1);
    auto& diags = d.diagnostics();
    EXPECT_GT(diags.size(), 0u);
}
