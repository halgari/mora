#include <gtest/gtest.h>

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/extension.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/reader_expansion.h"

#include <variant>

namespace {

mora::Module parse_source(const std::string& src, mora::StringPool& pool,
                          mora::DiagBag& diags) {
    mora::Lexer lexer(src, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    return parser.parse_module();
}

} // namespace

TEST(ReaderExpansionTest, RegisteredReaderReplacesTaggedLiteralNode) {
    mora::StringPool pool;
    mora::DiagBag diags;
    auto mod = parse_source(
        R"MORA(dummy(X):
    form/keyword(X, #answer "life")
)MORA",
        pool, diags);
    ASSERT_EQ(diags.error_count(), 0u);

    mora::ext::ExtensionContext ext_ctx;
    ext_ctx.register_reader("answer",
        [](mora::ext::ReaderContext& /*ctx*/,
           std::string_view /*payload*/,
           const mora::SourceSpan& span) -> mora::Expr {
            mora::Expr e;
            e.span = span;
            e.data = mora::IntLiteral{42, span};
            return e;
        });

    mora::ext::ReaderContext rctx{pool, diags, nullptr, nullptr};
    mora::expand_readers(mod, ext_ctx, rctx);

    ASSERT_EQ(mod.rules.size(), 1u);
    const auto* fp = std::get_if<mora::FactPattern>(&mod.rules[0].body[0].data);
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fp->args.size(), 2u);
    // The #answer tag got replaced with IntLiteral{42}.
    const auto* il = std::get_if<mora::IntLiteral>(&fp->args[1].data);
    ASSERT_NE(il, nullptr);
    EXPECT_EQ(il->value, 42);
}

TEST(ReaderExpansionTest, UnknownTagProducesDiagnostic) {
    mora::StringPool pool;
    mora::DiagBag diags;
    auto mod = parse_source(
        R"MORA(dummy(X):
    form/keyword(X, #nope "whatever")
)MORA",
        pool, diags);
    ASSERT_EQ(diags.error_count(), 0u);

    mora::ext::ExtensionContext ext_ctx;  // no readers registered
    mora::ext::ReaderContext rctx{pool, diags, nullptr, nullptr};
    mora::expand_readers(mod, ext_ctx, rctx);

    bool saw = false;
    for (auto& d : diags.all()) {
        if (d.code == "reader-unknown-tag") saw = true;
    }
    EXPECT_TRUE(saw);
}
