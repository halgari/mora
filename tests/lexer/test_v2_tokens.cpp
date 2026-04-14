#include "mora/lexer/lexer.h"
#include <gtest/gtest.h>

using namespace mora;

namespace {

std::vector<Token> tokenize(const std::string& src) {
    StringPool pool;
    DiagBag diags;
    Lexer lex(src, "test.mora", pool, diags);
    std::vector<Token> tokens;
    while (true) {
        auto tok = lex.next();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof) break;
    }
    return tokens;
}

} // namespace

TEST(V2Tokens, EditorIdTokenEmitted) {
    auto tokens = tokenize("@IronSword\n");
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::EditorId);
    EXPECT_EQ(tokens[0].text, "IronSword");
}

TEST(V2Tokens, EditorIdRequiresIdentifierAfterAt) {
    auto tokens = tokenize("@\n");
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Error);
}

TEST(V2Tokens, ColonIdentIsSymbolToken) {
    auto tokens = tokenize(":high\n");
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Symbol);
    EXPECT_EQ(tokens[0].text, "high");
}

TEST(V2Tokens, TokenKindNameForSymbolSaysKeyword) {
    EXPECT_STREQ(token_kind_name(TokenKind::Symbol), "Keyword");
}
