#pragma once
#include "mora/ast/types.h"
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <optional>

namespace mora {

// Forward declarations
struct Expr;

enum class RuleKind : uint8_t { Static, Maintain, On };
enum class VerbKind : uint8_t { Set, Add, Sub, Remove };

// ── Expressions ──
struct VariableExpr { StringId name; MoraType resolved_type = MoraType::make(TypeKind::Unknown); SourceSpan span; };
struct SymbolExpr   { StringId name; MoraType resolved_type = MoraType::make(TypeKind::Unknown); SourceSpan span; };
struct EditorIdExpr { StringId name; MoraType resolved_type = MoraType::make(TypeKind::Unknown); SourceSpan span; };
struct IntLiteral   { int64_t value; SourceSpan span; };
struct FloatLiteral { double value;  SourceSpan span; };
struct StringLiteral { StringId value; SourceSpan span; };
struct KeywordLiteral{ StringId value; SourceSpan span; };
struct DiscardExpr   { SourceSpan span; };

struct BinaryExpr {
    enum class Op { Add, Sub, Mul, Div, Eq, Neq, Lt, Gt, LtEq, GtEq };
    Op op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    SourceSpan span;
};

// Call to a built-in pure function: e.g. max(A, B), abs(N).
struct CallExpr {
    StringId name;
    std::vector<Expr> args;
    MoraType resolved_type = MoraType::make(TypeKind::Unknown);
    SourceSpan span;
};

struct Expr {
    std::variant<VariableExpr, SymbolExpr, EditorIdExpr, IntLiteral, FloatLiteral,
                 StringLiteral, KeywordLiteral, DiscardExpr, BinaryExpr, CallExpr> data;
    SourceSpan span;
};

// ── Clauses (rule body) ──
struct FactPattern {
    StringId name;
    StringId qualifier; // optional namespace
    std::vector<Expr> args;
    bool negated = false;
    SourceSpan span;
};

struct GuardClause { std::unique_ptr<Expr> expr; SourceSpan span; };

// or block: disjunction of clause groups (each group is AND'd internally)
// or:
//     race_of(NPC, :WolfRace)
//     race_of(NPC, :HuskyRace)
struct OrClause {
    std::vector<std::vector<FactPattern>> branches; // each branch is a set of AND'd facts
    SourceSpan span;
};

// Variable in [:A, :B, :C]  — set membership test
struct InClause {
    std::unique_ptr<Expr> variable;
    std::vector<Expr> values;
    SourceSpan span;
};

struct Effect {
    VerbKind  verb = VerbKind::Set;
    StringId  namespace_;   // e.g. "form"
    StringId  name;         // e.g. "keyword"
    std::vector<Expr> args;
    SourceSpan span;
};

struct ConditionalEffect {
    std::unique_ptr<Expr> guard;
    Effect effect;
    SourceSpan span;
};

struct Clause {
    std::variant<FactPattern, GuardClause, OrClause, InClause, Effect, ConditionalEffect> data;
    SourceSpan span;
};

// ── Top-level declarations ──
struct Rule {
    RuleKind kind = RuleKind::Static;
    StringId name;
    std::vector<Expr> head_args;
    std::vector<Clause> body;
    std::vector<Effect> effects;
    std::vector<ConditionalEffect> conditional_effects;
    SourceSpan span;
    std::optional<std::string> doc_comment; // leading # comments above the rule head
};

struct NamespaceDecl  { StringId name; SourceSpan span; };
struct RequiresDecl   { StringId mod_name; SourceSpan span; };
struct UseDecl        {
    StringId namespace_path;
    StringId alias;                     // 0 if no :as
    std::vector<StringId> refer;        // names from :refer [...] (empty if none)
    SourceSpan span;
};
struct ImportIniDecl  { enum class Kind { Spid, Kid }; Kind kind; StringId path; SourceSpan span; };
struct FactDecl       { StringId name; std::vector<MoraType> param_types; SourceSpan span; };

// ── Module (one .mora file) ──
struct Module {
    std::string filename;
    std::string source; // original source text (for diagnostic line display)
    std::optional<NamespaceDecl> ns;
    std::vector<RequiresDecl> requires_decls;
    std::vector<UseDecl> use_decls;
    std::vector<ImportIniDecl> import_decls;
    std::vector<FactDecl> fact_decls;
    std::vector<Rule> rules;

    // Extract a source line (1-indexed) for diagnostic display.
    // Uses a precomputed line offset table for O(1) lookup.
    std::string get_line(uint32_t line) const;

    // Build the line offset table from source. Called automatically by
    // get_line on first use, but can be called explicitly after setting source.
    void build_line_index() const;

private:
    mutable std::vector<size_t> line_offsets_; // byte offset of each line start
};

} // namespace mora
