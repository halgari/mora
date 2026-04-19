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

TEST(V2Tokens, ColonIdentIsKeywordToken) {
    auto tokens = tokenize(":high\n");
    ASSERT_GE(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Keyword);
    EXPECT_EQ(tokens[0].text, "high");
}

TEST(V2Tokens, TokenKindNameForKeywordSaysKeyword) {
    // Matches the lowercase convention used by other literal tokens
    // (integer, float, string, variable, identifier).
    EXPECT_STREQ(token_kind_name(TokenKind::Keyword), "keyword");
}

TEST(V2Tokens, NamespacedIdentifierLexesAsThreeTokens) {
    auto tokens = tokenize("form/keyword\n");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "form");
    EXPECT_EQ(tokens[1].kind, TokenKind::Slash);
    EXPECT_EQ(tokens[2].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[2].text, "keyword");
}

TEST(V2Tokens, NewKeywordsRecognized) {
    // Verb keywords (set/add/sub/remove) dropped in M1 — they lex as plain identifiers.
    auto tokens = tokenize("maintain on as refer\n");
    ASSERT_GE(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwMaintain);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwOn);
    EXPECT_EQ(tokens[2].kind, TokenKind::KwAs);
    EXPECT_EQ(tokens[3].kind, TokenKind::KwRefer);
}

TEST(V2Tokens, VerbWordsAreNowIdentifiers) {
    // set/add/sub/remove are no longer reserved — they lex as plain identifiers.
    auto tokens = tokenize("set add sub remove\n");
    ASSERT_GE(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[1].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[2].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[3].kind, TokenKind::Identifier);
}
