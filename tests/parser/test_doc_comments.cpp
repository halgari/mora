#include <gtest/gtest.h>
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

using namespace mora;

namespace {

Module parse_with_trivia(const std::string& src) {
    StringPool pool;
    DiagBag diags;
    Lexer lex(src, "x.mora", pool, diags);
    lex.set_keep_trivia(true);
    Parser parser(lex, pool, diags);
    return parser.parse_module();
}

} // namespace

TEST(ParserDocComments, AttachesAdjacentLeadingHashCommentsToRule) {
    std::string src =
        "namespace t\n"
        "\n"
        "# This rule tags bandits.\n"
        "# Multi-line block.\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n";
    auto mod = parse_with_trivia(src);
    ASSERT_EQ(mod.rules.size(), 1u);
    ASSERT_TRUE(mod.rules[0].doc_comment.has_value());
    EXPECT_EQ(*mod.rules[0].doc_comment,
              " This rule tags bandits.\n Multi-line block.");
}

TEST(ParserDocComments, NoDocCommentWhenBlankLineBetweenCommentAndHead) {
    std::string src =
        "namespace t\n"
        "\n"
        "# Stray comment.\n"
        "\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n";
    auto mod = parse_with_trivia(src);
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_FALSE(mod.rules[0].doc_comment.has_value());
}

TEST(ParserDocComments, DefaultLexerModeProducesNoDocComment) {
    std::string src =
        "namespace t\n"
        "# This rule tags bandits.\n"
        "bandit(NPC):\n"
        "    form/npc(NPC)\n";
    StringPool pool;
    DiagBag diags;
    Lexer lex(src, "x.mora", pool, diags);
    // NB: not calling set_keep_trivia
    Parser parser(lex, pool, diags);
    auto mod = parser.parse_module();
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_FALSE(mod.rules[0].doc_comment.has_value());
}
