#pragma once
#include "mora/ast/ast.h"
#include "mora/lexer/lexer.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

namespace mora {

class Parser {
public:
    Parser(Lexer& lexer, StringPool& pool, DiagBag& diags);
    Module parse_module();

private:
    Token peek();
    Token advance();
    Token expect(TokenKind kind, const std::string& msg);
    bool check(TokenKind kind);
    bool match(TokenKind kind);
    void skip_newlines();
    void synchronize();

    NamespaceDecl parse_namespace();
    RequiresDecl parse_requires();
    UseDecl parse_use();
    ImportIniDecl parse_import_ini(ImportIniDecl::Kind kind);

    Rule parse_rule();
    FactPattern parse_fact_pattern(bool negated = false);
    Effect parse_effect();

    Expr parse_expr();
    Expr parse_comparison();
    Expr parse_additive();
    Expr parse_primary();

    StringId parse_dotted_name();
    std::vector<Expr> parse_arg_list();

    Lexer& lexer_;
    StringPool& pool_;
    DiagBag& diags_;
    Token current_;
    bool has_current_ = false;
};

} // namespace mora
