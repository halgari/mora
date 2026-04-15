#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"

namespace mora {
struct Module;
class NameResolver;
class TypeChecker;
}

namespace mora::lsp {

enum class SymbolKind : uint8_t {
    RuleHead,        // The name in `bandit(NPC):`
    RuleCall,        // A call site, e.g. the `bandit` in `bandit(X)` inside a rule body
    Relation,        // A built-in relation reference, e.g. `weapon` in `form/weapon(W)`
    Atom,            // `@BanditFaction`
    VariableBinding, // First-occurrence of a variable in a rule
    VariableUse,     // Subsequent occurrence of a variable
    Namespace,       // Top of file: `namespace test.example`
};

struct SymbolEntry {
    SymbolKind  kind;
    SourceSpan  span;       // Span of just this token (not the surrounding construct)
    StringId    name;       // The symbol's name (interned)
    std::string ns_path;    // For Relation: "form" / "ref" / etc. For RuleCall: empty.
    // For variables, the binding-site span (same as span if this entry IS the binding).
    SourceSpan  binding_span;
};

class SymbolIndex {
public:
    // Build the index by walking a fully-resolved Module.
    void build(const mora::Module& mod,
               const mora::NameResolver& resolver,
               const mora::TypeChecker& tc,
               const mora::StringPool& pool);

    // Find the entry whose span contains (line, col). Returns nullptr if
    // nothing matches. Lines/cols are 1-based (mora convention).
    const SymbolEntry* find_at(uint32_t line, uint32_t col) const;

    // All entries — used by find-references / document-symbols.
    const std::vector<SymbolEntry>& entries() const { return entries_; }

    void clear() { entries_.clear(); }

private:
    std::vector<SymbolEntry> entries_;
};

} // namespace mora::lsp
