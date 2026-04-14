#include "mora/lexer/lexer.h"
#include <cctype>
#include <cstdlib>

namespace mora {

Lexer::Lexer(const std::string& source, const std::string& filename,
             StringPool& pool, DiagBag& diags)
    : source_(source), filename_(filename), pool_(pool), diags_(diags) {
    index_lines();
}

void Lexer::index_lines() {
    line_starts_.push_back(0); // line 1 starts at offset 0
    for (size_t i = 0; i < source_.size(); i++) {
        if (source_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

std::string_view Lexer::get_line(uint32_t line) const {
    if (line == 0 || line > line_starts_.size()) return {};
    size_t start = line_starts_[line - 1];
    size_t end;
    if (line < line_starts_.size()) {
        end = line_starts_[line];
        // Strip trailing newline
        if (end > start && source_[end - 1] == '\n') end--;
    } else {
        end = source_.size();
    }
    return std::string_view(source_).substr(start, end - start);
}

char Lexer::peek() const {
    if (at_end()) return '\0';
    return source_[pos_];
}

char Lexer::peek_next() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_];
    pos_++;
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

void Lexer::skip_whitespace_on_line() {
    while (!at_end() && (peek() == ' ' || peek() == '\t') ) {
        advance();
    }
}

void Lexer::skip_comment() {
    // Skip from # to end of line (don't consume the newline)
    while (!at_end() && peek() != '\n') {
        advance();
    }
}

Token Lexer::make_token(TokenKind kind) {
    std::string_view text(source_.data() + token_start_, pos_ - token_start_);
    SourceSpan span{filename_, token_start_line_, token_start_col_,
                    line_, col_};
    StringId sid = pool_.intern(text);
    Token tok;
    tok.kind = kind;
    tok.span = span;
    tok.text = pool_.get(sid); // stable pointer from pool
    tok.string_id = sid;
    return tok;
}

Token Lexer::make_token(TokenKind kind, int64_t int_val) {
    Token tok = make_token(kind);
    tok.int_value = int_val;
    return tok;
}

Token Lexer::make_token(TokenKind kind, double float_val) {
    Token tok = make_token(kind);
    tok.float_value = float_val;
    return tok;
}

Token Lexer::error_token(const std::string& msg) {
    SourceSpan span{filename_, token_start_line_, token_start_col_,
                    line_, col_};
    std::string src_line(get_line(token_start_line_));
    diags_.error("E0001", msg, span, src_line);
    std::string_view text(source_.data() + token_start_, pos_ - token_start_);
    StringId sid = pool_.intern(text);
    Token tok;
    tok.kind = TokenKind::Error;
    tok.span = span;
    tok.text = pool_.get(sid);
    tok.string_id = sid;
    return tok;
}

Token Lexer::handle_newline() {
    token_start_ = pos_;
    token_start_line_ = line_;
    token_start_col_ = col_;
    advance(); // consume '\n'
    at_line_start_ = true;
    return make_token(TokenKind::Newline);
}

Token Lexer::lex_identifier_or_keyword() {
    while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        advance();
    }

    std::string_view text(source_.data() + token_start_, pos_ - token_start_);

    // Discard: lone underscore
    if (text == "_") {
        return make_token(TokenKind::Discard);
    }

    // Keywords
    if (text == "namespace") return make_token(TokenKind::KwNamespace);
    if (text == "requires") return make_token(TokenKind::KwRequires);
    if (text == "mod") return make_token(TokenKind::KwMod);
    if (text == "use") return make_token(TokenKind::KwUse);
    if (text == "only") return make_token(TokenKind::KwOnly);
    if (text == "not") return make_token(TokenKind::KwNot);
    if (text == "or") return make_token(TokenKind::KwOr);
    if (text == "in") return make_token(TokenKind::KwIn);
    if (text == "import_spid") return make_token(TokenKind::KwImportSpid);
    if (text == "import_kid") return make_token(TokenKind::KwImportKid);
    if (text == "maintain") return make_token(TokenKind::KwMaintain);
    if (text == "on") return make_token(TokenKind::KwOn);
    if (text == "as") return make_token(TokenKind::KwAs);
    if (text == "refer") return make_token(TokenKind::KwRefer);
    if (text == "set") return make_token(TokenKind::KwSet);
    if (text == "add") return make_token(TokenKind::KwAdd);
    if (text == "sub") return make_token(TokenKind::KwSub);
    if (text == "remove") return make_token(TokenKind::KwRemove);

    // Variable: starts with uppercase
    if (std::isupper(static_cast<unsigned char>(text[0]))) {
        return make_token(TokenKind::Variable);
    }

    return make_token(TokenKind::Identifier);
}

Token Lexer::lex_number() {
    // Check for hex: 0x...
    if (peek() == '0' && (pos_ + 1 < source_.size()) &&
        (source_[pos_ + 1] == 'x' || source_[pos_ + 1] == 'X')) {
        advance(); // '0'
        advance(); // 'x'
        while (!at_end() && std::isxdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        std::string_view text(source_.data() + token_start_, pos_ - token_start_);
        int64_t val = static_cast<int64_t>(std::strtoll(std::string(text).c_str(), nullptr, 16));
        return make_token(TokenKind::Integer, val);
    }

    // Decimal integer or float
    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    // Check for float
    if (!at_end() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
        advance(); // consume '.'
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        std::string_view text(source_.data() + token_start_, pos_ - token_start_);
        double val = std::strtod(std::string(text).c_str(), nullptr);
        return make_token(TokenKind::Float, val);
    }

    std::string_view text(source_.data() + token_start_, pos_ - token_start_);
    int64_t val = static_cast<int64_t>(std::strtoll(std::string(text).c_str(), nullptr, 10));
    return make_token(TokenKind::Integer, val);
}

Token Lexer::lex_string() {
    advance(); // consume opening '"'
    while (!at_end() && peek() != '"' && peek() != '\n') {
        if (peek() == '\\') advance(); // skip escape char
        advance();
    }
    if (at_end() || peek() == '\n') {
        return error_token("unterminated string literal");
    }
    advance(); // consume closing '"'
    return make_token(TokenKind::String);
}

Token Lexer::lex_symbol() {
    advance(); // consume ':'
    if (!at_end() && std::isalpha(static_cast<unsigned char>(peek()))) {
        size_t ident_start = pos_;
        while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }
        // Build token with text = identifier only (stripping ':').
        // Span still covers the ':' + identifier range.
        size_t saved_start = token_start_;
        token_start_ = ident_start;
        Token tok = make_token(TokenKind::Symbol);
        token_start_ = saved_start;
        return tok;
    }
    // Check for double colon
    if (!at_end() && peek() == ':') {
        advance();
        return make_token(TokenKind::DoubleColon);
    }
    return make_token(TokenKind::Colon);
}

Token Lexer::next() {
    // Emit pending dedents first
    if (pending_dedents_ > 0) {
        pending_dedents_--;
        token_start_ = pos_;
        token_start_line_ = line_;
        token_start_col_ = col_;
        return make_token(TokenKind::Dedent);
    }

    // Handle indentation at line start
    if (at_line_start_) {
        at_line_start_ = false;

        // Count spaces at start of line
        size_t indent_start = pos_;
        int indent = 0;
        while (!at_end() && (peek() == ' ' || peek() == '\t')) {
            if (peek() == '\t') {
                indent += 4;
            } else {
                indent += 1;
            }
            advance();
        }

        // Skip blank lines and comment-only lines - don't change indentation
        if (at_end() || peek() == '\n' || peek() == '#') {
            // Don't change indent for blank/comment lines
            // If it's a comment, skip it
            if (!at_end() && peek() == '#') {
                skip_comment();
            }
            // If there's a newline, we'll pick it up on the next call
            // If at end, fall through to EOF handling below
            if (!at_end() && peek() == '\n') {
                return handle_newline();
            }
            // At EOF, fall through
        } else {
            // Real content line - check indent changes
            int current_indent = indent_stack_.back();
            if (indent > current_indent) {
                indent_stack_.push_back(indent);
                token_start_ = indent_start;
                token_start_line_ = line_;
                token_start_col_ = 1;
                return make_token(TokenKind::Indent);
            } else if (indent < current_indent) {
                // Pop indent levels and queue dedents
                int dedent_count = 0;
                while (indent_stack_.size() > 1 && indent_stack_.back() > indent) {
                    indent_stack_.pop_back();
                    dedent_count++;
                }
                if (dedent_count > 0) {
                    pending_dedents_ = dedent_count - 1; // -1 because we emit one now
                    token_start_ = indent_start;
                    token_start_line_ = line_;
                    token_start_col_ = 1;
                    return make_token(TokenKind::Dedent);
                }
            }
            // indent == current_indent: no token needed
        }
    }

    // Skip horizontal whitespace
    skip_whitespace_on_line();

    // Record token start
    token_start_ = pos_;
    token_start_line_ = line_;
    token_start_col_ = col_;

    if (at_end()) {
        // Emit remaining dedents at EOF
        if (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            pending_dedents_ = static_cast<int>(indent_stack_.size()) - 1;
            return make_token(TokenKind::Dedent);
        }
        return make_token(TokenKind::Eof);
    }

    char c = peek();

    // Comment
    if (c == '#') {
        skip_comment();
        // After comment, check if at end or newline
        if (!at_end() && peek() == '\n') {
            return handle_newline();
        }
        // At EOF after comment
        token_start_ = pos_;
        token_start_line_ = line_;
        token_start_col_ = col_;
        if (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            pending_dedents_ = static_cast<int>(indent_stack_.size()) - 1;
            return make_token(TokenKind::Dedent);
        }
        return make_token(TokenKind::Eof);
    }

    // Newline
    if (c == '\n') {
        return handle_newline();
    }

    // Identifier / keyword / variable / discard
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return lex_identifier_or_keyword();
    }

    // Number
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return lex_number();
    }

    // String
    if (c == '"') {
        return lex_string();
    }

    // Symbol or colon
    if (c == ':') {
        return lex_symbol();
    }

    // EditorId: @ident
    if (c == '@') {
        advance(); // consume '@'
        if (!at_end() && (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_')) {
            size_t ident_start = pos_;
            while (!at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
                advance();
            }
            // Build token with text = identifier only (stripping '@').
            // Span still covers the '@' + identifier range.
            size_t saved_start = token_start_;
            token_start_ = ident_start;
            Token tok = make_token(TokenKind::EditorId);
            token_start_ = saved_start;
            return tok;
        }
        return error_token("expected identifier after '@'");
    }

    // Two-character operators
    if (c == '=') {
        advance();
        if (match('>')) return make_token(TokenKind::Arrow);
        if (match('=')) return make_token(TokenKind::Eq);
        // Bare '=' - error for now
        return error_token("unexpected character '='");
    }

    if (c == '!') {
        advance();
        if (match('=')) return make_token(TokenKind::Neq);
        return error_token("unexpected character '!'");
    }

    if (c == '<') {
        advance();
        if (match('=')) return make_token(TokenKind::LtEq);
        return make_token(TokenKind::Lt);
    }

    if (c == '>') {
        advance();
        if (match('=')) return make_token(TokenKind::GtEq);
        return make_token(TokenKind::Gt);
    }

    // Single-character tokens
    advance();
    switch (c) {
    case '(': return make_token(TokenKind::LParen);
    case ')': return make_token(TokenKind::RParen);
    case '[': return make_token(TokenKind::LBracket);
    case ']': return make_token(TokenKind::RBracket);
    case ',': return make_token(TokenKind::Comma);
    case '.': return make_token(TokenKind::Dot);
    case '+': return make_token(TokenKind::Plus);
    case '-': return make_token(TokenKind::Minus);
    case '*': return make_token(TokenKind::Star);
    case '/': return make_token(TokenKind::Slash);
    case '|': return make_token(TokenKind::Pipe);
    default:
        return error_token(std::string("unexpected character '") + c + "'");
    }
}

} // namespace mora
