#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

using namespace mora;

namespace {
std::vector<TokenKind> kinds(Lexer& lex) {
    std::vector<TokenKind> out;
    for (;;) {
        Token t = lex.next();
        out.push_back(t.kind);
        if (t.kind == TokenKind::Eof) break;
    }
    return out;
}
} // namespace

TEST(LexerCommentTrivia, DefaultModeSkipsComments) {
    StringPool pool;
    DiagBag diags;
    std::string src = "# leading\nnamespace foo\n";
    Lexer lex(src, "x.mora", pool, diags);
    auto ks = kinds(lex);
    // No Comment token expected.
    for (auto k : ks) EXPECT_NE(k, TokenKind::Comment);
}

TEST(LexerCommentTrivia, KeepTriviaEmitsComment) {
    StringPool pool;
    DiagBag diags;
    std::string src = "# leading\nnamespace foo\n";
    Lexer lex(src, "x.mora", pool, diags);
    lex.set_keep_trivia(true);
    auto ks = kinds(lex);
    bool saw_comment = false;
    for (auto k : ks) if (k == TokenKind::Comment) saw_comment = true;
    EXPECT_TRUE(saw_comment);
}

TEST(LexerCommentTrivia, CommentExcludesHashSymbol) {
    StringPool pool;
    DiagBag diags;
    std::string src = "# foo bar";
    Lexer lex(src, "x.mora", pool, diags);
    lex.set_keep_trivia(true);
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::Comment);
    // The token text should be " foo bar" (no leading '#').
    auto text = pool.get(t.string_id);
    EXPECT_EQ(text, " foo bar");
}
