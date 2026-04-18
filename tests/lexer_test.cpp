#include <gtest/gtest.h>
#include "mora/lexer/lexer.h"

class LexerTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    std::vector<mora::Token> lex(const std::string& source) {
        mora::Lexer lexer(source, "test.mora", pool, diags);
        std::vector<mora::Token> tokens;
        while (true) {
            auto tok = lexer.next();
            tokens.push_back(tok);
            if (tok.kind == mora::TokenKind::Eof) break;
        }
        return tokens;
    }

    // Helper: collect non-whitespace token kinds
    std::vector<mora::TokenKind> kinds(const std::string& source) {
        auto tokens = lex(source);
        std::vector<mora::TokenKind> result;
        for (auto& t : tokens) {
            if (t.kind != mora::TokenKind::Newline &&
                t.kind != mora::TokenKind::Indent &&
                t.kind != mora::TokenKind::Dedent) {
                result.push_back(t.kind);
            }
        }
        return result;
    }
};

TEST_F(LexerTest, EmptyInput) {
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Eof);
}

TEST_F(LexerTest, SimpleIdentifier) {
    auto tokens = lex("bandit");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "bandit");
}

TEST_F(LexerTest, Variable) {
    auto tokens = lex("NPC");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Variable);
    EXPECT_EQ(tokens[0].text, "NPC");
}

TEST_F(LexerTest, Symbol) {
    auto tokens = lex(":BanditFaction");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Keyword);
    EXPECT_EQ(tokens[0].text, "BanditFaction");
}

TEST_F(LexerTest, Keywords) {
    EXPECT_EQ(lex("namespace")[0].kind, mora::TokenKind::KwNamespace);
    EXPECT_EQ(lex("requires")[0].kind, mora::TokenKind::KwRequires);
    EXPECT_EQ(lex("use")[0].kind, mora::TokenKind::KwUse);
    EXPECT_EQ(lex("not")[0].kind, mora::TokenKind::KwNot);
    EXPECT_EQ(lex("only")[0].kind, mora::TokenKind::KwOnly);
    EXPECT_EQ(lex("mod")[0].kind, mora::TokenKind::KwMod);
}

TEST_F(LexerTest, Integer) {
    auto tokens = lex("42");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Integer);
    EXPECT_EQ(tokens[0].int_value, 42);
}

TEST_F(LexerTest, HexInteger) {
    auto tokens = lex("0x013BB9");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Integer);
    EXPECT_EQ(tokens[0].int_value, 0x013BB9);
}

TEST_F(LexerTest, Float) {
    auto tokens = lex("3.14");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Float);
    EXPECT_DOUBLE_EQ(tokens[0].float_value, 3.14);
}

TEST_F(LexerTest, StringLiteral) {
    auto tokens = lex("\"hello world\"");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::String);
    EXPECT_EQ(tokens[0].text, "\"hello world\"");
}

TEST_F(LexerTest, Operators) {
    auto k = kinds("== != < > <= >= + - * /");
    ASSERT_EQ(k.size(), 11u); // 10 operators + Eof
    EXPECT_EQ(k[0], mora::TokenKind::Eq);
    EXPECT_EQ(k[1], mora::TokenKind::Neq);
    EXPECT_EQ(k[2], mora::TokenKind::Lt);
    EXPECT_EQ(k[3], mora::TokenKind::Gt);
    EXPECT_EQ(k[4], mora::TokenKind::LtEq);
    EXPECT_EQ(k[5], mora::TokenKind::GtEq);
    EXPECT_EQ(k[6], mora::TokenKind::Plus);
    EXPECT_EQ(k[7], mora::TokenKind::Minus);
    EXPECT_EQ(k[8], mora::TokenKind::Star);
    EXPECT_EQ(k[9], mora::TokenKind::Slash);
}

TEST_F(LexerTest, ArrowIsError) {
    // => no longer exists as a token — bare '=' is an error token.
    auto tokens = lex("=>");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Error);
}

TEST_F(LexerTest, Punctuation) {
    auto k = kinds("( ) [ ] , .");
    EXPECT_EQ(k[0], mora::TokenKind::LParen);
    EXPECT_EQ(k[1], mora::TokenKind::RParen);
    EXPECT_EQ(k[2], mora::TokenKind::LBracket);
    EXPECT_EQ(k[3], mora::TokenKind::RBracket);
    EXPECT_EQ(k[4], mora::TokenKind::Comma);
    EXPECT_EQ(k[5], mora::TokenKind::Dot);
}

TEST_F(LexerTest, CommentSkipped) {
    auto tokens = lex("bandit # this is a comment");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "bandit");
}

TEST_F(LexerTest, Discard) {
    auto tokens = lex("_");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Discard);
}

TEST_F(LexerTest, IndentDedent) {
    auto tokens = lex("rule:\n    body\n");
    std::vector<mora::TokenKind> expected = {
        mora::TokenKind::Identifier,   // rule
        mora::TokenKind::Colon,        // :
        mora::TokenKind::Newline,      // \n
        mora::TokenKind::Indent,       // increase
        mora::TokenKind::Identifier,   // body
        mora::TokenKind::Newline,      // \n
        mora::TokenKind::Dedent,       // decrease
        mora::TokenKind::Eof,
    };
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(tokens[i].kind, expected[i])
            << "token " << i << ": expected " << mora::token_kind_name(expected[i])
            << ", got " << mora::token_kind_name(tokens[i].kind);
    }
}

TEST_F(LexerTest, FullRuleLex) {
    std::string source =
        "skyrim/add(Weapon, :Keyword, @VampireBane):\n"
        "    form/weapon(Weapon)\n"
        "    form/keyword(Weapon, @WeapMaterialSilver)\n";

    auto tokens = lex(source);
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "skyrim");
    EXPECT_EQ(tokens[1].kind, mora::TokenKind::Slash);
    EXPECT_EQ(tokens[2].kind, mora::TokenKind::Identifier);
    EXPECT_EQ(tokens[2].text, "add");
    EXPECT_FALSE(diags.has_errors());
}

TEST_F(LexerTest, ErrorOnUnexpectedCharacter) {
    lex("@");
    EXPECT_TRUE(diags.has_errors());
}

TEST_F(LexerTest, SymbolWithPlugin) {
    auto tokens = lex(":Skyrim");
    EXPECT_EQ(tokens[0].kind, mora::TokenKind::Keyword);
    EXPECT_EQ(tokens[0].text, "Skyrim");
}
