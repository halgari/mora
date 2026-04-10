#pragma once
#include "mora/lexer/token.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <string>
#include <string_view>
#include <vector>

namespace mora {

class Lexer {
public:
    Lexer(const std::string& source, const std::string& filename,
          StringPool& pool, DiagBag& diags);

    Token next();
    std::string_view get_line(uint32_t line) const;

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool match(char expected);
    bool at_end() const;

    void skip_whitespace_on_line();
    void skip_comment();

    Token make_token(TokenKind kind);
    Token make_token(TokenKind kind, int64_t int_val);
    Token make_token(TokenKind kind, double float_val);
    Token error_token(const std::string& msg);

    Token lex_identifier_or_keyword();
    Token lex_number();
    Token lex_string();
    Token lex_symbol();
    Token handle_newline();

    std::string source_;
    std::string filename_;
    StringPool& pool_;
    DiagBag& diags_;

    size_t pos_ = 0;
    size_t token_start_ = 0;
    uint32_t line_ = 1;
    uint32_t col_ = 1;
    uint32_t token_start_line_ = 1;
    uint32_t token_start_col_ = 1;

    // Indentation tracking
    std::vector<int> indent_stack_ = {0};
    int pending_dedents_ = 0;
    bool at_line_start_ = true;

    // Line index for get_line()
    std::vector<size_t> line_starts_;
    void index_lines();
};

} // namespace mora
