#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "mora/core/string_pool.h"
#include "mora/data/schema_registry.h"
#include "mora/lsp/editor_id_registry.h"

namespace mora::lsp {

class Document;

class Workspace {
public:
    Workspace();
    ~Workspace();

    bool initialized() const { return initialized_; }
    void mark_initialized() { initialized_ = true; }

    bool shutdown_requested() const { return shutdown_requested_; }
    void request_shutdown() { shutdown_requested_ = true; }

    // Document lifecycle.
    Document* open(std::string uri, std::string text, int version);
    void      change(std::string_view uri, std::string text, int version);
    void      close(std::string_view uri);
    Document* get(std::string_view uri);

    // For tests.
    size_t document_count() const { return docs_.size(); }

    // Append an outgoing notification to be sent at the end of the
    // current dispatch tick.
    void enqueue_notification(nlohmann::json msg) { outgoing_.push_back(std::move(msg)); }

    // Drain queued notifications. The run loop calls this after each
    // dispatch and writes them out.
    std::vector<nlohmann::json> drain_outgoing() {
        std::vector<nlohmann::json> out;
        out.swap(outgoing_);
        return out;
    }

    // SchemaRegistry — loaded on initialize (or on set_relations_dir).
    // Workspace owns a StringPool for interning; both are long-lived.
    void set_relations_dir(std::filesystem::path dir);
    const mora::SchemaRegistry& schema() const { return *schema_; }

    // Expose the schema's StringPool so callers can cross-intern names
    // from a document pool (which uses a separate pool) into the schema pool.
    mora::StringPool& schema_pool() { return *schema_pool_; }

    // The directory from which relations YAML files are served (set during initialize).
    const std::filesystem::path& relations_dir() const { return relations_dir_; }

    // EditorIdRegistry — best-effort atom (@Foo) → FormID resolution.
    // scan() is triggered by set_data_dir(); returns nullopt if unloaded.
    void set_data_dir(std::filesystem::path dir);
    const EditorIdRegistry& editor_ids() const { return *editor_ids_; }

    // Iterate all open documents — used by find-references, workspace-symbol,
    // and goto-def-across-files. Returns raw pointers; documents are owned by docs_.
    std::vector<Document*> all_documents() {
        std::vector<Document*> out;
        out.reserve(docs_.size());
        for (auto& [uri, doc] : docs_) out.push_back(doc.get());
        return out;
    }

    // Returns documents whose reparse_after deadline has passed and which
    // still have a stale parse cache. Caller is responsible for calling
    // diagnostics() on each (which clears the staleness) and publishing.
    std::vector<Document*> documents_due_for_reparse(std::chrono::steady_clock::time_point now);

private:
    bool initialized_ = false;
    bool shutdown_requested_ = false;
    std::unordered_map<std::string, std::unique_ptr<Document>> docs_;
    std::vector<nlohmann::json> outgoing_;

    // Owned string pool for the shared SchemaRegistry.
    // Using unique_ptr because StringPool is not move-assignable (has a mutex).
    std::unique_ptr<mora::StringPool> schema_pool_;
    std::unique_ptr<mora::SchemaRegistry> schema_;
    std::filesystem::path relations_dir_;

    // EditorIdRegistry for atom resolution (stub until real ESP scan lands).
    std::unique_ptr<EditorIdRegistry> editor_ids_;
};

} // namespace mora::lsp
