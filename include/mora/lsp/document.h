#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mora/diag/diagnostic.h"
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/lsp/symbol_index.h"

namespace mora::lsp {

class Workspace;

class Document {
public:
    Document(const Workspace& ws, std::string uri, std::string text, int version);

    const std::string& uri() const     { return uri_; }
    const std::string& text() const    { return text_; }
    int                version() const { return version_; }

    // Replace the text and bump the version. Marks the parse cache stale.
    void update(std::string text, int version);

    // Re-run the parse pipeline if the cache is stale. Returns the latest
    // diagnostics scoped to this document's URI.
    const std::vector<mora::Diagnostic>& diagnostics();

    // Schedule a re-parse to happen no sooner than `at`. Used by debouncing.
    void schedule_reparse(std::chrono::steady_clock::time_point at) { reparse_after_ = at; }
    bool reparse_due(std::chrono::steady_clock::time_point now) const {
        return cache_stale_ && now >= reparse_after_;
    }

    // Phase-3 accessors. All return references valid until the next reparse.
    const SymbolIndex&       symbol_index() const { return index_; }
    const mora::StringPool&  pool()         const { return *pool_; }
    const mora::Module&      module()       const { return *module_; }

    // Lookup a Rule by its interned name. Returns nullptr if not found.
    const mora::Rule* find_rule_by_name(mora::StringId name) const;

private:
    const Workspace& ws_;
    std::string uri_;
    std::string text_;
    int version_ = 0;

    bool cache_stale_ = true;
    std::chrono::steady_clock::time_point reparse_after_{};
    std::vector<mora::Diagnostic> diagnostics_;

    // Phase-3: keep parse outputs alive between reparses so semantic queries
    // can read them after diagnostics() returns.
    std::unique_ptr<mora::StringPool> pool_;
    std::unique_ptr<mora::Module>     module_;
    SymbolIndex                        index_;
};

} // namespace mora::lsp
