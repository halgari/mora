#include <filesystem>
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

nlohmann::json location_json(std::string_view uri, const mora::SourceSpan& span) {
    return {
        {"uri",   std::string(uri)},
        {"range", span_to_range(span)},
    };
}

// Try to find a rule head for `name_str` in any workspace document.
// Uses string comparison to avoid cross-pool StringId issues.
nlohmann::json find_rule_definition(Workspace& ws,
                                    std::string_view name_str) {
    for (Document const* d : ws.all_documents()) {
        for (const auto& rule : d->module().rules) {
            if (d->pool().get(rule.name) == name_str) {
                return location_json(d->uri(), rule.span);
            }
        }
    }
    return nlohmann::json(nullptr);
}

// Build an Option-B path for a relation and return it if it exists on disk.
// Path: <relations_dir>/<ns>/<name>.yaml
// If relations_dir is empty or file doesn't exist, returns null.
nlohmann::json find_relation_definition(Workspace& ws,
                                        const SymbolEntry& entry,
                                        const mora::StringPool& doc_pool) {
    const std::filesystem::path& rel_dir = ws.relations_dir();
    if (rel_dir.empty()) return nlohmann::json(nullptr);

    std::string const name_str = std::string(doc_pool.get(entry.name));
    std::filesystem::path yaml_path;
    if (entry.ns_path.empty()) {
        yaml_path = rel_dir / (name_str + ".yaml");
    } else {
        yaml_path = rel_dir / entry.ns_path / (name_str + ".yaml");
    }

    if (!std::filesystem::exists(yaml_path)) return nlohmann::json(nullptr);

    // Return a location at line 0, col 0 of the YAML file.
    // We don't parse YAML here; line 0 is a reasonable jump target.
    std::string file_uri = "file://" + yaml_path.string();
    nlohmann::json range = {
        {"start", {{"line", 0}, {"character", 0}}},
        {"end",   {{"line", 0}, {"character", 0}}},
    };
    return nlohmann::json{{"uri", file_uri}, {"range", range}};
}

Result on_definition(Workspace& ws, const nlohmann::json& params) {
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

    switch (entry->kind) {
        case SymbolKind::RuleHead: {
            // Jump to own definition — the rule head itself.
            std::string_view const name_str = doc->pool().get(entry->name);
            return find_rule_definition(ws, name_str);
        }
        case SymbolKind::RuleCall: {
            std::string_view const name_str = doc->pool().get(entry->name);
            return find_rule_definition(ws, name_str);
        }
        case SymbolKind::Relation: {
            return find_relation_definition(ws, *entry, doc->pool());
        }
        case SymbolKind::VariableBinding: {
            // Already at the binding site.
            return location_json(doc->uri(), entry->span);
        }
        case SymbolKind::VariableUse: {
            // Jump to the binding span.
            return location_json(doc->uri(), entry->binding_span);
        }
        case SymbolKind::Atom:
        case SymbolKind::Namespace:
            return nlohmann::json(nullptr);
    }
    return nlohmann::json(nullptr);
}

} // namespace

void register_definition_handler(Dispatcher& d) {
    d.on_request("textDocument/definition", on_definition);
}

} // namespace mora::lsp
