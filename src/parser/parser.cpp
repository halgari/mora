#include "mora/parser/parser.h"
#include <sstream>

namespace mora {

Parser::Parser(Lexer& lexer, StringPool& pool, DiagBag& diags)
    : lexer_(lexer), pool_(pool), diags_(diags), current_() {}

// ── Token helpers ──

Token Parser::peek() {
    if (!has_current_) {
        current_ = lexer_.next();
        has_current_ = true;
    }
    return current_;
}

Token Parser::advance() {
    Token tok = peek();
    has_current_ = false;
    return tok;
}

Token Parser::expect(TokenKind kind, const std::string& msg) {
    Token tok = peek();
    if (tok.kind == kind) {
        return advance();
    }
    diags_.error("P0001", msg, tok.span, "");
    // Return the token as-is (don't consume) so caller can decide
    return tok;
}

bool Parser::check(TokenKind kind) {
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

void Parser::skip_newlines() {
    while (check(TokenKind::Newline)) {
        advance();
    }
}

void Parser::synchronize() {
    // Skip tokens until we find a top-level position:
    // an identifier, keyword, or EOF at the base indent level.
    while (!check(TokenKind::Eof)) {
        TokenKind k = peek().kind;
        if (k == TokenKind::Dedent) {
            advance();
            continue;
        }
        if (k == TokenKind::Newline) {
            advance();
            continue;
        }
        // If we see a top-level keyword or identifier after newlines/dedents, stop
        if (k == TokenKind::Identifier || k == TokenKind::KwNamespace ||
            k == TokenKind::KwRequires || k == TokenKind::KwUse ||
            k == TokenKind::KwImportSpid || k == TokenKind::KwImportKid ||
            k == TokenKind::KwMaintain || k == TokenKind::KwOn) {
            break;
        }
        advance();
    }
}

// ── Module parsing ──

Module Parser::parse_module() {
    Module mod;
    mod.filename = "test.mora";

    skip_newlines();

    while (!check(TokenKind::Eof)) {
        TokenKind k = peek().kind;

        if (k == TokenKind::Newline) {
            advance();
            continue;
        }

        if (k == TokenKind::KwNamespace) {
            mod.ns = parse_namespace();
            skip_newlines();
            continue;
        }

        if (k == TokenKind::KwRequires) {
            mod.requires_decls.push_back(parse_requires());
            skip_newlines();
            continue;
        }

        if (k == TokenKind::KwUse) {
            mod.use_decls.push_back(parse_use());
            skip_newlines();
            continue;
        }

        if (k == TokenKind::KwImportSpid) {
            mod.import_decls.push_back(parse_import_ini(ImportIniDecl::Kind::Spid));
            skip_newlines();
            continue;
        }

        if (k == TokenKind::KwImportKid) {
            mod.import_decls.push_back(parse_import_ini(ImportIniDecl::Kind::Kid));
            skip_newlines();
            continue;
        }

        if (k == TokenKind::Identifier || k == TokenKind::KwMaintain ||
            k == TokenKind::KwOn) {
            mod.rules.push_back(parse_rule());
            skip_newlines();
            continue;
        }

        // Unexpected token
        diags_.error("P0002", "unexpected token at top level", peek().span, "");
        advance();
    }

    return mod;
}

// ── Top-level declarations ──

NamespaceDecl Parser::parse_namespace() {
    Token kw = advance(); // consume 'namespace'
    StringId name = parse_dotted_name();
    NamespaceDecl decl;
    decl.name = name;
    decl.span = kw.span;
    return decl;
}

RequiresDecl Parser::parse_requires() {
    Token kw = advance(); // consume 'requires'
    expect(TokenKind::KwMod, "expected 'mod' after 'requires'");
    expect(TokenKind::LParen, "expected '(' after 'mod'");
    Token str_tok = expect(TokenKind::String, "expected string literal");
    // Strip quotes from string: the lexer includes them in the text,
    // but the string_id should be the content without quotes.
    // Actually, let's check what the lexer does with strings.
    // The lexer's lex_string strips quotes and stores the unquoted content.
    // But we need to verify. The text field includes quotes but string_id
    // is interned from the text including quotes. Let's handle both cases.
    std::string_view sv = pool_.get(str_tok.string_id);
    std::string stripped;
    if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"') {
        stripped = std::string(sv.substr(1, sv.size() - 2));
    } else {
        stripped = std::string(sv);
    }
    StringId mod_name = pool_.intern(stripped);
    expect(TokenKind::RParen, "expected ')' after string");

    RequiresDecl decl;
    decl.mod_name = mod_name;
    decl.span = kw.span;
    return decl;
}

UseDecl Parser::parse_use() {
    Token kw = advance(); // consume 'use'
    StringId ns_path = parse_dotted_name();

    UseDecl decl;
    decl.namespace_path = ns_path;
    decl.span = kw.span;

    if (match(TokenKind::KwOnly)) {
        expect(TokenKind::LBracket, "expected '[' after 'only'");
        while (!check(TokenKind::RBracket) && !check(TokenKind::Eof) && !check(TokenKind::Newline)) {
            Token name_tok = expect(TokenKind::Identifier, "expected identifier in only list");
            if (name_tok.kind == TokenKind::Identifier) {
                decl.refer.push_back(name_tok.string_id);
            }
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RBracket, "expected ']' after only list");
    }

    // Handle v2 :as / :refer clauses.  The lexer emits `:foo` as a single
    // Symbol token whose string_id holds "foo", so we inspect the text to
    // dispatch between :as and :refer.
    while (check(TokenKind::Symbol)) {
        Token sym = peek();
        std::string_view text = pool_.get(sym.string_id);
        if (text == "as") {
            advance();
            Token alias_tok = expect(TokenKind::Identifier,
                "expected alias identifier after ':as'");
            if (alias_tok.kind == TokenKind::Identifier) {
                decl.alias = alias_tok.string_id;
            }
        } else if (text == "refer") {
            advance();
            expect(TokenKind::LBracket, "expected '[' after ':refer'");
            while (!check(TokenKind::RBracket) && !check(TokenKind::Eof) &&
                   !check(TokenKind::Newline)) {
                Token name_tok = expect(TokenKind::Identifier,
                    "expected identifier in :refer list");
                if (name_tok.kind == TokenKind::Identifier) {
                    decl.refer.push_back(name_tok.string_id);
                }
                if (!match(TokenKind::Comma)) {
                    break;
                }
            }
            expect(TokenKind::RBracket, "expected ']' after :refer list");
        } else {
            break;
        }
    }

    return decl;
}

ImportIniDecl Parser::parse_import_ini(ImportIniDecl::Kind kind) {
    Token kw = advance(); // consume keyword
    Token path_tok = expect(TokenKind::String, "expected string path");

    ImportIniDecl decl;
    decl.kind = kind;
    decl.path = path_tok.string_id;
    decl.span = kw.span;
    return decl;
}

// ── Rule parsing ──

Rule Parser::parse_rule() {
    Rule rule;

    // Optional rule-kind annotation: 'maintain' or 'on' precedes the rule name.
    if (check(TokenKind::KwMaintain)) {
        advance();
        rule.kind = RuleKind::Maintain;
    } else if (check(TokenKind::KwOn)) {
        advance();
        rule.kind = RuleKind::On;
    }

    Token name_tok = expect(TokenKind::Identifier, "expected rule name");
    rule.name = name_tok.string_id;
    rule.span = name_tok.span;

    expect(TokenKind::LParen, "expected '(' after rule name");
    rule.head_args = parse_arg_list();
    expect(TokenKind::RParen, "expected ')' after rule arguments");
    expect(TokenKind::Colon, "expected ':' after rule head");

    // Expect newline then indent
    if (!match(TokenKind::Newline)) {
        // Try to recover
        diags_.error("P0003", "expected newline after ':'", peek().span, "");
    }

    if (!match(TokenKind::Indent)) {
        // No body - empty rule or error
        return rule;
    }

    // Parse body lines until Dedent or Eof
    while (!check(TokenKind::Dedent) && !check(TokenKind::Eof)) {
        if (check(TokenKind::Newline)) {
            advance();
            continue;
        }

        // => effect (bare effect)
        if (check(TokenKind::Arrow)) {
            advance(); // consume '=>'
            rule.effects.push_back(parse_effect());
            skip_newlines();
            continue;
        }

        // not <fact_pattern> (negated fact)
        if (check(TokenKind::KwNot)) {
            advance(); // consume 'not'
            FactPattern fp = parse_fact_pattern(true);
            Clause clause;
            clause.data = std::move(fp);
            clause.span = std::get<FactPattern>(clause.data).span;
            rule.body.push_back(std::move(clause));
            skip_newlines();
            continue;
        }

        // identifier followed by '(' -> fact pattern
        // identifier '/' identifier '(' -> namespaced fact pattern
        if (check(TokenKind::Identifier)) {
            // Look ahead: is next token '(' ?
            // We need a two-token lookahead. Save state.
            Token id_tok = advance();
            StringId qualifier{};
            StringId fact_name = id_tok.string_id;
            SourceSpan fact_span = id_tok.span;

            if (check(TokenKind::Slash)) {
                advance(); // consume '/'
                Token name_tok = expect(TokenKind::Identifier,
                    "expected identifier after '/' in namespaced fact");
                qualifier = id_tok.string_id;
                fact_name = name_tok.string_id;
                fact_span = merge_spans(id_tok.span, name_tok.span);
            }

            if (check(TokenKind::LParen)) {
                // It's a fact pattern. We already consumed the identifier(s).
                advance(); // consume '('
                std::vector<Expr> args = parse_arg_list();
                expect(TokenKind::RParen, "expected ')' in fact pattern");

                FactPattern fp;
                fp.name = fact_name;
                fp.qualifier = qualifier;
                fp.args = std::move(args);
                fp.negated = false;
                fp.span = fact_span;

                Clause clause;
                clause.data = std::move(fp);
                clause.span = fact_span;
                rule.body.push_back(std::move(clause));
                skip_newlines();
                continue;
            } else {
                // Not a fact pattern. Must be something else - but identifiers
                // at rule body level that aren't followed by '(' are unexpected.
                // Treat as error and skip.
                diags_.error("P0004", "expected '(' after identifier in rule body", peek().span, "");
                synchronize();
                continue;
            }
        }

        // Variable -> could be a guard clause or conditional effect
        if (check(TokenKind::Variable)) {
            Expr expr = parse_expr();

            // Check if followed by '=>' -> conditional effect
            if (check(TokenKind::Arrow)) {
                advance(); // consume '=>'
                Effect eff = parse_effect();
                ConditionalEffect ce;
                ce.guard = std::make_unique<Expr>(std::move(expr));
                ce.effect = std::move(eff);
                ce.span = ce.guard->span;
                rule.conditional_effects.push_back(std::move(ce));
            } else {
                // Just a guard clause
                GuardClause gc;
                gc.span = expr.span;
                gc.expr = std::make_unique<Expr>(std::move(expr));
                Clause clause;
                clause.span = gc.span;
                clause.data = std::move(gc);
                rule.body.push_back(std::move(clause));
            }
            skip_newlines();
            continue;
        }

        // Error token or unexpected - try to recover
        if (check(TokenKind::Error)) {
            advance();
            synchronize();
            continue;
        }

        // Unexpected token in rule body
        diags_.error("P0005", "unexpected token in rule body", peek().span, "");
        advance();
    }

    // Consume Dedent
    match(TokenKind::Dedent);

    return rule;
}

FactPattern Parser::parse_fact_pattern(bool negated) {
    Token name_tok = expect(TokenKind::Identifier, "expected fact name");

    FactPattern fp;
    fp.name = name_tok.string_id;
    fp.negated = negated;
    fp.span = name_tok.span;

    if (check(TokenKind::Slash)) {
        advance(); // consume '/'
        Token ns_name_tok = expect(TokenKind::Identifier,
            "expected identifier after '/' in namespaced fact");
        fp.qualifier = name_tok.string_id;
        fp.name = ns_name_tok.string_id;
        fp.span = merge_spans(name_tok.span, ns_name_tok.span);
    }

    expect(TokenKind::LParen, "expected '(' after fact name");
    fp.args = parse_arg_list();
    expect(TokenKind::RParen, "expected ')' in fact pattern");

    return fp;
}

Effect Parser::parse_effect() {
    Effect eff;

    // Expect a verb keyword: set | add | sub | remove
    VerbKind verb = VerbKind::Set;
    SourceSpan start_span = peek().span;
    switch (peek().kind) {
        case TokenKind::KwSet:    verb = VerbKind::Set;    advance(); break;
        case TokenKind::KwAdd:    verb = VerbKind::Add;    advance(); break;
        case TokenKind::KwSub:    verb = VerbKind::Sub;    advance(); break;
        case TokenKind::KwRemove: verb = VerbKind::Remove; advance(); break;
        default:
            diags_.error("P0007",
                "expected verb (set/add/sub/remove) after '=>'",
                peek().span, "");
            break;
    }
    eff.verb = verb;

    // Parse namespaced name: identifier '/' identifier
    Token ns_tok = expect(TokenKind::Identifier, "expected namespace identifier");
    expect(TokenKind::Slash, "expected '/' after effect namespace");
    Token name_tok = expect(TokenKind::Identifier, "expected effect name after '/'");

    eff.namespace_ = ns_tok.string_id;
    eff.name = name_tok.string_id;

    expect(TokenKind::LParen, "expected '(' after effect name");
    eff.args = parse_arg_list();
    Token rparen = peek();
    expect(TokenKind::RParen, "expected ')' in effect");

    eff.span = merge_spans(start_span, rparen.span);
    return eff;
}

// ── Expression parsing ──

Expr Parser::parse_expr() {
    return parse_comparison();
}

Expr Parser::parse_comparison() {
    Expr left = parse_additive();

    while (check(TokenKind::Eq) || check(TokenKind::Neq) ||
           check(TokenKind::Lt) || check(TokenKind::Gt) ||
           check(TokenKind::LtEq) || check(TokenKind::GtEq)) {
        Token op_tok = advance();
        BinaryExpr::Op op;
        switch (op_tok.kind) {
            case TokenKind::Eq:   op = BinaryExpr::Op::Eq;   break;
            case TokenKind::Neq:  op = BinaryExpr::Op::Neq;  break;
            case TokenKind::Lt:   op = BinaryExpr::Op::Lt;   break;
            case TokenKind::Gt:   op = BinaryExpr::Op::Gt;   break;
            case TokenKind::LtEq: op = BinaryExpr::Op::LtEq; break;
            case TokenKind::GtEq: op = BinaryExpr::Op::GtEq; break;
            default: __builtin_unreachable();
        }
        Expr right = parse_additive();

        BinaryExpr bin;
        bin.op = op;
        bin.span = merge_spans(left.span, right.span);
        bin.left = std::make_unique<Expr>(std::move(left));
        bin.right = std::make_unique<Expr>(std::move(right));

        Expr result;
        result.span = bin.span;
        result.data = std::move(bin);
        left = std::move(result);
    }

    return left;
}

Expr Parser::parse_additive() {
    Expr left = parse_multiplicative();

    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
        Token op_tok = advance();
        BinaryExpr::Op op;
        switch (op_tok.kind) {
            case TokenKind::Plus:  op = BinaryExpr::Op::Add; break;
            case TokenKind::Minus: op = BinaryExpr::Op::Sub; break;
            default: __builtin_unreachable();
        }
        Expr right = parse_multiplicative();

        BinaryExpr bin;
        bin.op = op;
        bin.span = merge_spans(left.span, right.span);
        bin.left = std::make_unique<Expr>(std::move(left));
        bin.right = std::make_unique<Expr>(std::move(right));

        Expr result;
        result.span = bin.span;
        result.data = std::move(bin);
        left = std::move(result);
    }

    return left;
}

Expr Parser::parse_multiplicative() {
    Expr left = parse_primary();

    while (check(TokenKind::Star) || check(TokenKind::Slash)) {
        Token op_tok = advance();
        BinaryExpr::Op op;
        switch (op_tok.kind) {
            case TokenKind::Star:  op = BinaryExpr::Op::Mul; break;
            case TokenKind::Slash: op = BinaryExpr::Op::Div; break;
            default: __builtin_unreachable();
        }
        Expr right = parse_primary();

        BinaryExpr bin;
        bin.op = op;
        bin.span = merge_spans(left.span, right.span);
        bin.left = std::make_unique<Expr>(std::move(left));
        bin.right = std::make_unique<Expr>(std::move(right));

        Expr result;
        result.span = bin.span;
        result.data = std::move(bin);
        left = std::move(result);
    }

    return left;
}

Expr Parser::parse_primary() {
    Token tok = peek();

    if (tok.kind == TokenKind::Variable) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = VariableExpr{tok.string_id, MoraType::make(TypeKind::Unknown), tok.span};
        return e;
    }

    if (tok.kind == TokenKind::Symbol) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = SymbolExpr{tok.string_id, MoraType::make(TypeKind::Unknown), tok.span};
        return e;
    }

    if (tok.kind == TokenKind::EditorId) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = EditorIdExpr{tok.string_id, MoraType::make(TypeKind::Unknown), tok.span};
        return e;
    }

    if (tok.kind == TokenKind::Integer) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = IntLiteral{tok.int_value, tok.span};
        return e;
    }

    if (tok.kind == TokenKind::Float) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = FloatLiteral{tok.float_value, tok.span};
        return e;
    }

    if (tok.kind == TokenKind::String) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = StringLiteral{tok.string_id, tok.span};
        return e;
    }

    if (tok.kind == TokenKind::Discard) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = DiscardExpr{tok.span};
        return e;
    }

    if (tok.kind == TokenKind::LParen) {
        advance(); // consume '('
        Expr inner = parse_expr();
        expect(TokenKind::RParen, "expected ')' after expression");
        return inner;
    }

    // If we get an identifier in expression context, treat as variable-like
    // This shouldn't normally happen but helps with error recovery
    if (tok.kind == TokenKind::Identifier) {
        advance();
        Expr e;
        e.span = tok.span;
        e.data = VariableExpr{tok.string_id, MoraType::make(TypeKind::Unknown), tok.span};
        return e;
    }

    // Error: unexpected token
    diags_.error("P0006", "expected expression", tok.span, "");
    advance();
    Expr e;
    e.span = tok.span;
    e.data = DiscardExpr{tok.span};
    return e;
}

// ── Helpers ──

StringId Parser::parse_dotted_name() {
    Token first = expect(TokenKind::Identifier, "expected name");
    if (first.kind != TokenKind::Identifier) {
        return first.string_id; // error already reported
    }

    std::string name(pool_.get(first.string_id));

    while (check(TokenKind::Dot)) {
        advance(); // consume '.'
        Token part = expect(TokenKind::Identifier, "expected name after '.'");
        if (part.kind == TokenKind::Identifier) {
            name += '.';
            name += pool_.get(part.string_id);
        }
    }

    return pool_.intern(name);
}

std::vector<Expr> Parser::parse_arg_list() {
    std::vector<Expr> args;

    if (check(TokenKind::RParen)) {
        return args;
    }

    args.push_back(parse_expr());
    while (match(TokenKind::Comma)) {
        args.push_back(parse_expr());
    }

    return args;
}

} // namespace mora
