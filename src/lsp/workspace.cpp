#include "mora/lsp/workspace.h"
#include "mora/lsp/document.h"
#include "mora/data/schema_registry.h"
#include <filesystem>

namespace mora::lsp {

Workspace::Workspace()
    : schema_pool_(std::make_unique<mora::StringPool>()),
      schema_(std::make_unique<mora::SchemaRegistry>(*schema_pool_)),
      editor_ids_(std::make_unique<EditorIdRegistry>()) {}

Workspace::~Workspace() = default;

void Workspace::set_relations_dir(std::filesystem::path dir) {
    relations_dir_ = std::move(dir);
    // Rebuild the registry. The built-in Skyrim relations are compiled
    // into the binary via src/model/relations_seed.cpp (generated from
    // data/relations/**/*.yaml). There is no runtime load_directory():
    // the YAML has already been pre-processed at build time.
    schema_pool_ = std::make_unique<mora::StringPool>();
    schema_ = std::make_unique<mora::SchemaRegistry>(*schema_pool_);
    schema_->register_defaults();  // always — defaults are compiled in
    // The relations_dir_ path is used by definition.cpp for goto-def
    // YAML resolution; the registry itself doesn't read from disk.
}

void Workspace::set_data_dir(const std::filesystem::path& dir) {
    editor_ids_->scan(dir);
}

std::vector<Document*> Workspace::documents_due_for_reparse(
    std::chrono::steady_clock::time_point now) {
    std::vector<Document*> out;
    for (auto& [uri, doc] : docs_) {
        if (doc->reparse_due(now)) out.push_back(doc.get());
    }
    return out;
}

Document* Workspace::open(std::string uri, std::string text, int version) {
    auto doc = std::make_unique<Document>(*this, uri, std::move(text), version);
    Document* raw = doc.get();
    docs_.emplace(std::move(uri), std::move(doc));
    return raw;
}

void Workspace::change(std::string_view uri, std::string text, int version) {
    auto it = docs_.find(std::string(uri));
    if (it != docs_.end()) it->second->update(std::move(text), version);
}

void Workspace::close(std::string_view uri) {
    docs_.erase(std::string(uri));
}

Document* Workspace::get(std::string_view uri) {
    auto it = docs_.find(std::string(uri));
    return it == docs_.end() ? nullptr : it->second.get();
}

} // namespace mora::lsp
