#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/lexer/lexer.h"

#include <gtest/gtest.h>

namespace {

TEST(LexerKeyword, TokenizesColonIdent) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::Lexer lex(":GoldValue :Name",  "test", pool, diags);

    auto t1 = lex.next();
    EXPECT_EQ(t1.kind, mora::TokenKind::Keyword);
    EXPECT_EQ(pool.get(t1.string_id), "GoldValue");

    auto t2 = lex.next();
    EXPECT_EQ(t2.kind, mora::TokenKind::Keyword);
    EXPECT_EQ(pool.get(t2.string_id), "Name");
}

TEST(LexerKeyword, BareColonStillColon) {
    mora::StringPool pool;
    mora::DiagBag diags;
    // Bare `:` unambiguously — not followed by an identifier-start char.
    mora::Lexer lex(": ", "test", pool, diags);

    auto t = lex.next();
    EXPECT_EQ(t.kind, mora::TokenKind::Colon);
}

TEST(LexerKeyword, DoubleColonStillDoubleColon) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::Lexer lex("::foo", "test", pool, diags);

    auto t1 = lex.next();
    EXPECT_EQ(t1.kind, mora::TokenKind::DoubleColon);

    auto t2 = lex.next();
    EXPECT_EQ(t2.kind, mora::TokenKind::Identifier);
}

} // namespace
