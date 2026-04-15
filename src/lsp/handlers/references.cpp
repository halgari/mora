#include <string_view>

#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

namespace {

// 0-based LSP line/char → 1-based mora convention.
uint32_t to_one_based(int v) {
    return v < 0 ? 1 : static_cast<uint32_t>(v + 1);
}

// Convert a 1-based SourceSpan to a 0-based LSP range JSON object.
nlohmann::json span_to_range(const mora::SourceSpan& span) {
    auto zb = [](uint32_t one) -> int {
        return one == 0 ? 0 : static_cast<int>(one - 1);
    };
    return {
        {"start", {{"line", zb(span.start_line)}, {"character", zb(span.start_col)}}},
        {"end",   {{"line", zb(span.end_line)},   {"character", zb(span.end_col)}}},
    };
}

// Determine whether `candidate` is a reference to `target`.
// Uses string-based name comparison (passed as `target_name_str`) to handle
// cross-document string pool differences. The `target_doc_pool` is the pool
// of the document that owns `target`.
bool entries_match_for_refs(const SymbolEntry& target,
                             std::string_view target_name_str,
                             const SymbolEntry& candidate,
                             const mora::StringPool& candidate_pool) {
    // Name must match (by string value, pool-agnostic).
    if (candidate_pool.get(candidate.name) != target_name_str) return false;

    // Variables: scoped per rule via binding_span identity.
    if (target.kind == SymbolKind::VariableBinding ||
        target.kind == SymbolKind::VariableUse) {
        return (candidate.kind == SymbolKind::VariableBinding ||
                candidate.kind == SymbolKind::VariableUse) &&
               candidate.binding_span.start_line == target.binding_span.start_line &&
               candidate.binding_span.start_col  == target.binding_span.start_col;
    }

    // Rules: match any RuleHead or RuleCall with the same name.
    // Also match Relation entries with no namespace prefix — a user-defined
    // rule call in a document that didn't resolve the name ends up as a
    // Relation entry (resolver doesn't know about cross-document rules).
    if (target.kind == SymbolKind::RuleHead || target.kind == SymbolKind::RuleCall) {
        if (candidate.kind == SymbolKind::RuleHead ||
            candidate.kind == SymbolKind::RuleCall) {
            return true;
        }
        // Cross-document: unresolved rule call emitted as Relation with empty ns_path.
        if (candidate.kind == SymbolKind::Relation && candidate.ns_path.empty()) {
            return true;
        }
        return false;
    }

    // Relations: match by name and namespace prefix.
    if (target.kind == SymbolKind::Relation) {
        return candidate.kind == SymbolKind::Relation &&
               candidate.ns_path == target.ns_path;
    }

    // Atoms: match other atoms with the same name.
    if (target.kind == SymbolKind::Atom) {
        return candidate.kind == SymbolKind::Atom;
    }

    return false;
}

Result on_references(Workspace& ws, const nlohmann::json& params) {
    std::string uri = params.at("textDocument").at("uri").get<std::string>();
    int line_zb = params.at("position").at("line").get<int>();
    int char_zb = params.at("position").at("character").get<int>();
    bool include_decl = true;
    if (params.contains("context") && params["context"].contains("includeDeclaration")) {
        include_decl = params["context"]["includeDeclaration"].get<bool>();
    }

    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json::array();

    // Ensure parse is up to date.
    (void)doc->diagnostics();

    const SymbolEntry* target = doc->symbol_index().find_at(
        to_one_based(line_zb), to_one_based(char_zb));
    if (!target) return nlohmann::json::array();

    // Resolve the target name to a string once, for cross-pool comparison.
    std::string_view target_name_str = doc->pool().get(target->name);

    nlohmann::json refs = nlohmann::json::array();

    auto add = [&](const Document& d, const SymbolEntry& e) {
        if (!include_decl &&
            (e.kind == SymbolKind::RuleHead || e.kind == SymbolKind::VariableBinding)) {
            return;
        }
        refs.push_back({
            {"uri",   d.uri()},
            {"range", span_to_range(e.span)},
        });
    };

    // Variables: scoped to the current document only (single-rule binding scope).
    if (target->kind == SymbolKind::VariableBinding ||
        target->kind == SymbolKind::VariableUse) {
        for (const auto& e : doc->symbol_index().entries()) {
            if (entries_match_for_refs(*target, target_name_str, e, doc->pool())) {
                add(*doc, e);
            }
        }
        return refs;
    }

    // All other kinds: workspace-wide scan.
    for (Document* d : ws.all_documents()) {
        // Ensure each document's index is populated.
        (void)d->diagnostics();
        for (const auto& e : d->symbol_index().entries()) {
            if (entries_match_for_refs(*target, target_name_str, e, d->pool())) {
                add(*d, e);
            }
        }
    }
    return refs;
}

} // namespace

void register_references_handler(Dispatcher& d) {
    d.on_request("textDocument/references", on_references);
}

} // namespace mora::lsp
