#pragma once
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"
#include <string>
#include <string_view>

namespace mora {

enum class TokenKind {
    // Literals
    Integer, Float, String, Symbol, Variable, Identifier, Discard,

    // Keywords
    KwNamespace, KwRequires, KwMod, KwUse, KwOnly, KwNot, KwImportSpid, KwImportKid,

    // Punctuation
    Colon, DoubleColon, Comma, Dot, LParen, RParen, LBracket, RBracket,
    Arrow, Pipe, Hash,

    // Comparison operators
    Eq, Neq, Lt, Gt, LtEq, GtEq,

    // Arithmetic operators
    Plus, Minus, Star, Slash,

    // Whitespace/structure
    Newline, Indent, Dedent,

    // Special
    Eof, Error,
};

struct Token {
    TokenKind kind;
    SourceSpan span;
    std::string_view text;
    int64_t int_value = 0;
    double float_value = 0.0;
    StringId string_id;
};

const char* token_kind_name(TokenKind kind);

} // namespace mora
