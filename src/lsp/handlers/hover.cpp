#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "mora/lsp/dispatch.h"
#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/lsp/symbol_index.h"
#include "mora/lsp/editor_id_registry.h"
#include "mora/data/schema_registry.h"
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"

namespace mora::lsp {

namespace {

// 0-based LSP line/char → 1-based mora convention.
uint32_t to_one_based(int v) {
    return v < 0 ? 1 : static_cast<uint32_t>(v + 1);
}

// Build a human-readable parameter list from a Rule's head_args using the
// document's string pool.  Returns "(Arg1, Arg2)" or "" if no args.
std::string format_head_args(const mora::Rule& rule, const mora::StringPool& pool) {
    if (rule.head_args.empty()) return "()";
    std::string out = "(";
    bool first = true;
    for (const auto& expr : rule.head_args) {
        if (!first) out += ", ";
        first = false;
        // head args in a rule definition are VariableExpr nodes
        if (const auto* v = std::get_if<mora::VariableExpr>(&expr.data)) {
            out += std::string(pool.get(v->name));
        } else {
            out += "?";
        }
    }
    out += ")";
    return out;
}

std::string hover_for_rule(const SymbolEntry& e, const Document& doc) {
    const mora::StringPool& pool = doc.pool();
    const mora::Rule* rule = doc.find_rule_by_name(e.name);

    std::string const rule_name = std::string(pool.get(e.name));
    std::string sig = rule_name;
    if (rule) {
        sig += format_head_args(*rule, pool);
    }

    std::string md = "```mora\n" + sig + "\n```";
    if (rule && rule->doc_comment) {
        md += "\n\n";
        md += *rule->doc_comment;
    }
    return md;
}

std::string hover_for_relation(const SymbolEntry& e, const Workspace& ws,
                               const mora::StringPool& doc_pool) {
    // Get the relation name string from the document's pool.
    std::string_view const rel_name_sv = doc_pool.get(e.name);
    std::string const full_name = e.ns_path.empty()
        ? std::string(rel_name_sv)
        : e.ns_path + "/" + std::string(rel_name_sv);

    std::string md = "```mora\n" + full_name + "\n```";

    // Cross-intern: look up the schema via the workspace's own pool.
    // doc_pool and ws.schema_pool() are separate; we must re-intern.
    mora::StringId const schema_id = const_cast<Workspace&>(ws).schema_pool().intern(full_name);
    const mora::RelationSchema* schema = ws.schema().lookup(schema_id);
    if (schema) {
        md += "\n\nBuilt-in relation with " +
              std::to_string(schema->column_types.size()) + " column(s).";
    }
    return md;
}

std::string hover_for_atom(const SymbolEntry& e, const Workspace& ws,
                           const mora::StringPool& doc_pool) {
    std::string const id = std::string(doc_pool.get(e.name));
    std::string md = "```mora\n@" + id + "\n```";

    auto info = ws.editor_ids().lookup(id);
    if (info) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%08X", info->form_id);
        md += "\n\nEditor ID `" + id + "` → FormID `" + std::string(buf) +
              "` from `" + info->source_plugin +
              "` (record `" + info->record_type + "`)";
    } else {
        md += "\n\n`@" + id + "` — atom not resolved (data dir not loaded).";
    }
    return md;
}

std::string hover_for_variable(const SymbolEntry& e, const mora::StringPool& doc_pool) {
    std::string const name = std::string(doc_pool.get(e.name));
    std::string md = "```mora\n" + name + "\n```";
    if (e.kind == SymbolKind::VariableUse) {
        md += "\n\nBound on line " + std::to_string(e.binding_span.start_line) + ".";
    } else {
        md += "\n\n(Binding site)";
    }
    return md;
}

Result on_hover(Workspace& ws, const nlohmann::json& params) {
    std::string const uri = params.at("textDocument").at("uri").get<std::string>();
    int const line_zb = params.at("position").at("line").get<int>();
    int const char_zb = params.at("position").at("character").get<int>();

    Document* doc = ws.get(uri);
    if (!doc) return nlohmann::json(nullptr);

    // Ensure parse is up to date.
    (void)doc->diagnostics();

    const SymbolEntry* entry = doc->symbol_index().find_at(
        to_one_based(line_zb), to_one_based(char_zb));
    if (!entry) return nlohmann::json(nullptr);

    std::string md;
    switch (entry->kind) {
        case SymbolKind::RuleHead:
        case SymbolKind::RuleCall:
            md = hover_for_rule(*entry, *doc);
            break;
        case SymbolKind::Relation:
            md = hover_for_relation(*entry, ws, doc->pool());
            break;
        case SymbolKind::Atom:
            md = hover_for_atom(*entry, ws, doc->pool());
            break;
        case SymbolKind::VariableBinding:
        case SymbolKind::VariableUse:
            md = hover_for_variable(*entry, doc->pool());
            break;
        case SymbolKind::Namespace:
            return nlohmann::json(nullptr);
    }

    return nlohmann::json{
        {"contents", {{"kind", "markdown"}, {"value", md}}},
    };
}

} // namespace

void register_hover_handler(Dispatcher& d) {
    d.on_request("textDocument/hover", on_hover);
}

} // namespace mora::lsp
