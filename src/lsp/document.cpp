#include "mora/lsp/document.h"

#include "mora/lsp/uri.h"
#include "mora/lsp/workspace.h"
#include "mora/core/string_pool.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"

namespace mora::lsp {

Document::Document(const Workspace& ws, std::string uri, std::string text, int version)
    : ws_(ws)
    , uri_(std::move(uri))
    , text_(std::move(text))
    , version_(version)
    // Initialize persistent parse outputs so accessors are safe before first parse.
    , pool_(std::make_unique<mora::StringPool>())
    , module_(std::make_unique<mora::Module>()) {}

void Document::update(std::string text, int version) {
    text_ = std::move(text);
    version_ = version;
    cache_stale_ = true;
}

const std::vector<mora::Diagnostic>& Document::diagnostics() {
    if (!cache_stale_) return diagnostics_;

    std::string const path = path_from_uri(uri_);

    // Allocate pool and module on the heap so they survive past this scope.
    // StringPool is not movable (contains shared_mutex), so we heap-allocate
    // from the start rather than move-constructing into a unique_ptr later.
    auto pool   = std::make_unique<mora::StringPool>();
    auto module = std::make_unique<mora::Module>();

    mora::DiagBag bag;
    mora::Lexer lex(text_, path, *pool, bag);
    // Enable trivia so the parser can attach doc-comments to rules.
    lex.set_keep_trivia(true);
    mora::Parser parser(lex, *pool, bag);
    *module = parser.parse_module();
    module->filename = path;
    module->source = text_;

    // Sema passes mirror the check pipeline in src/main.cpp.
    // The workspace's shared SchemaRegistry (ws_.schema()) is available
    // for future use when sema passes accept it.
    mora::NameResolver resolver(*pool, bag);
    resolver.resolve(*module);

    mora::TypeChecker tc(*pool, bag, resolver);
    tc.check(*module);

    // Build the symbol index while sema objects (resolver, tc) are still alive.
    index_.build(*module, resolver, tc, *pool);

    // Hand off ownership so Phase-3 accessors remain valid after return.
    pool_   = std::move(pool);
    module_ = std::move(module);

    diagnostics_ = bag.drain_for_file(path);
    cache_stale_ = false;
    return diagnostics_;
}

const mora::Rule* Document::find_rule_by_name(mora::StringId name) const {
    if (!module_) return nullptr;
    for (const auto& rule : module_->rules) {
        if (rule.name == name) return &rule;
    }
    return nullptr;
}

} // namespace mora::lsp
