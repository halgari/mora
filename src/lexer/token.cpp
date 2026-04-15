#include "mora/lexer/token.h"

namespace mora {

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
    // Literals
    case TokenKind::Integer:
        return "integer";
    case TokenKind::Float:
        return "float";
    case TokenKind::String:
        return "string";
    case TokenKind::Symbol:
        return "Keyword";
    case TokenKind::EditorId:
        return "EditorId";
    case TokenKind::Variable:
        return "variable";
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::Discard:
        return "_";

    // Keywords
    case TokenKind::KwNamespace:
        return "namespace";
    case TokenKind::KwRequires:
        return "requires";
    case TokenKind::KwMod:
        return "mod";
    case TokenKind::KwUse:
        return "use";
    case TokenKind::KwOnly:
        return "only";
    case TokenKind::KwNot:
        return "not";
    case TokenKind::KwOr:
        return "or";
    case TokenKind::KwIn:
        return "in";
    case TokenKind::KwImportSpid:
        return "import_spid";
    case TokenKind::KwImportKid:
        return "import_kid";
    case TokenKind::KwMaintain:
        return "maintain";
    case TokenKind::KwOn:
        return "on";
    case TokenKind::KwAs:
        return "as";
    case TokenKind::KwRefer:
        return "refer";
    case TokenKind::KwSet:
        return "set";
    case TokenKind::KwAdd:
        return "add";
    case TokenKind::KwSub:
        return "sub";
    case TokenKind::KwRemove:
        return "remove";

    // Punctuation
    case TokenKind::Colon:
        return ":";
    case TokenKind::DoubleColon:
        return "::";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Dot:
        return ".";
    case TokenKind::LParen:
        return "(";
    case TokenKind::RParen:
        return ")";
    case TokenKind::LBracket:
        return "[";
    case TokenKind::RBracket:
        return "]";
    case TokenKind::Arrow:
        return "=>";
    case TokenKind::Pipe:
        return "|";
    case TokenKind::Hash:
        return "#";

    // Comparison operators
    case TokenKind::Eq:
        return "==";
    case TokenKind::Neq:
        return "!=";
    case TokenKind::Lt:
        return "<";
    case TokenKind::Gt:
        return ">";
    case TokenKind::LtEq:
        return "<=";
    case TokenKind::GtEq:
        return ">=";

    // Arithmetic operators
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Star:
        return "*";
    case TokenKind::Slash:
        return "/";

    // Trivia
    case TokenKind::Comment:
        return "Comment";

    // Whitespace/structure
    case TokenKind::Newline:
        return "newline";
    case TokenKind::Indent:
        return "indent";
    case TokenKind::Dedent:
        return "dedent";

    // Special
    case TokenKind::Eof:
        return "eof";
    case TokenKind::Error:
        return "error";
    }
    return "unknown";
}

} // namespace mora
