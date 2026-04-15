#include <gtest/gtest.h>
#include "mora/lsp/symbol_index.h"
#include "mora/lsp/document.h"
#include "mora/lsp/workspace.h"

using namespace mora::lsp;

namespace {
// Helper: open a document containing `src`, force a parse, return the index.
const SymbolIndex& parse_and_index(Workspace& ws, const std::string& src) {
    Document* doc = ws.open("file:///x.mora", src, 1);
    (void)doc->diagnostics();  // forces parse + index build
    return doc->symbol_index();
}
} // namespace

TEST(LspSymbolIndex, FindsRuleHead) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n");
    // bandit head is on line 2; rule.span starts at col 1.
    const auto* e = idx.find_at(2, 1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->kind, SymbolKind::RuleHead);
}

TEST(LspSymbolIndex, FindsRelationRefAndNamespace) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n");
    // form/npc — the `npc` portion is a Relation entry.
    // The fact pattern starts at col 5 (4 spaces indent).
    // qualifier "form" + "/" = 5 chars, so "npc" starts at col 10.
    // But our fact_name_span uses fp.span.start_col, which is the qualifier start (col 5).
    // Look for any Relation entry on line 3.
    const SymbolEntry* relation = nullptr;
    for (const auto& e : idx.entries()) {
        if (e.kind == SymbolKind::Relation && e.span.start_line == 3) {
            relation = &e;
            break;
        }
    }
    ASSERT_NE(relation, nullptr);
    EXPECT_EQ(relation->kind, SymbolKind::Relation);
    EXPECT_EQ(relation->ns_path, "form");
}

TEST(LspSymbolIndex, FindsVariableBindingAndUse) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n");

    // Scan entries for a VariableBinding on line 2 (head arg NPC).
    const SymbolEntry* binding = nullptr;
    for (const auto& e : idx.entries()) {
        if (e.kind == SymbolKind::VariableBinding && e.span.start_line == 2) {
            binding = &e;
            break;
        }
    }
    ASSERT_NE(binding, nullptr);
    EXPECT_EQ(binding->kind, SymbolKind::VariableBinding);

    // Scan entries for a VariableUse on line 3 (body arg NPC).
    const SymbolEntry* use = nullptr;
    for (const auto& e : idx.entries()) {
        if (e.kind == SymbolKind::VariableUse && e.span.start_line == 3) {
            use = &e;
            break;
        }
    }
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(use->kind, SymbolKind::VariableUse);
    // Use's binding_span should point back at the binding (line 2).
    EXPECT_EQ(use->binding_span.start_line, 2u);
}

TEST(LspSymbolIndex, FindsAtom) {
    Workspace ws;
    const auto& idx = parse_and_index(ws,
        "namespace t\n"
        "bandit(NPC):\n"
        "    form/keyword(NPC, @BanditFaction)\n");
    // Scan all entries for an Atom entry.
    const SymbolEntry* atom = nullptr;
    for (const auto& e : idx.entries()) {
        if (e.kind == SymbolKind::Atom) { atom = &e; break; }
    }
    ASSERT_NE(atom, nullptr);
}
