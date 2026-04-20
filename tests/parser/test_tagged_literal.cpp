#include <gtest/gtest.h>

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"

#include <variant>

namespace {

mora::Module parse_source(const std::string& src, mora::StringPool& pool,
                          mora::DiagBag& diags) {
    mora::Lexer lexer(src, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    return parser.parse_module();
}

const mora::FactPattern* first_fact(const mora::Rule& r) {
    for (auto const& c : r.body) {
        if (auto const* fp = std::get_if<mora::FactPattern>(&c.data)) {
            return fp;
        }
    }
    return nullptr;
}

} // namespace

TEST(TaggedLiteralParserTest, ArgPositionProducesTaggedLiteralExpr) {
    mora::StringPool pool;
    mora::DiagBag diags;
    auto mod = parse_source(
        R"MORA(skyrim/add(X, :Keyword, #form "0x800@Mod.esp"):
    form/weapon(X)
)MORA",
        pool, diags);

    EXPECT_EQ(diags.error_count(), 0u);
    ASSERT_EQ(mod.rules.size(), 1u);
    ASSERT_EQ(mod.rules[0].head_args.size(), 3u);

    const auto* tl = std::get_if<mora::TaggedLiteralExpr>(
        &mod.rules[0].head_args[2].data);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(pool.get(tl->tag), "form");
    EXPECT_EQ(pool.get(tl->payload), "0x800@Mod.esp");
}

TEST(TaggedLiteralParserTest, InBodyFactArg) {
    mora::StringPool pool;
    mora::DiagBag diags;
    auto mod = parse_source(
        R"MORA(tagged(X):
    form/keyword(X, #form "MyKW")
)MORA",
        pool, diags);

    EXPECT_EQ(diags.error_count(), 0u);
    ASSERT_EQ(mod.rules.size(), 1u);
    const auto* fp = first_fact(mod.rules[0]);
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fp->args.size(), 2u);
    const auto* tl = std::get_if<mora::TaggedLiteralExpr>(&fp->args[1].data);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(pool.get(tl->tag), "form");
    EXPECT_EQ(pool.get(tl->payload), "MyKW");
}

TEST(TaggedLiteralParserTest, HashFollowedBySpaceStillComment) {
    // Regression guard: legacy `# comment` syntax must keep working.
    mora::StringPool pool;
    mora::DiagBag diags;
    auto mod = parse_source(
        R"MORA(# this is a comment
tagless(X):
    form/weapon(X)
)MORA",
        pool, diags);

    EXPECT_EQ(diags.error_count(), 0u);
    ASSERT_EQ(mod.rules.size(), 1u);
    EXPECT_EQ(pool.get(mod.rules[0].name), "tagless");
}

TEST(TaggedLiteralParserTest, MalformedTagFailsGracefully) {
    // `#foo` with no payload — parser should emit an error, not crash.
    mora::StringPool pool;
    mora::DiagBag diags;
    (void)parse_source(
        R"MORA(skyrim/add(X, :Keyword, #foo):
    form/weapon(X)
)MORA",
        pool, diags);

    EXPECT_GT(diags.error_count(), 0u);
}
