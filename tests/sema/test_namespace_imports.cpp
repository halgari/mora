#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include <gtest/gtest.h>

using namespace mora;

namespace {

Module parse_src(StringPool& pool, DiagBag& diags, const char* src) {
    Lexer lex(src, "test.mora", pool, diags);
    Parser p(lex, pool, diags);
    return p.parse_module();
}

} // namespace

TEST(NamespaceImports, BareNameResolvedViaRefer) {
    StringPool pool;
    DiagBag diags;
    Module m = parse_src(pool, diags,
        "namespace x.y\n"
        "use ref :refer [keyword]\n"
        "r(R):\n"
        "    keyword(R, @X)\n");
    ASSERT_FALSE(diags.has_errors());
    NameResolver nr(pool, diags);
    nr.resolve(m);
    EXPECT_EQ(diags.error_count(), 0u);
    ASSERT_EQ(m.rules.size(), 1u);
    ASSERT_GE(m.rules[0].body.size(), 1u);
    const auto& fp = std::get<FactPattern>(m.rules[0].body[0].data);
    EXPECT_EQ(pool.get(fp.qualifier), "ref");
    EXPECT_EQ(pool.get(fp.name), "keyword");
}

TEST(NamespaceImports, AliasRewritesQualifier) {
    StringPool pool;
    DiagBag diags;
    Module m = parse_src(pool, diags,
        "namespace x.y\n"
        "use form :as f\n"
        "r(W):\n"
        "    f/weapon(W)\n");
    ASSERT_FALSE(diags.has_errors());
    NameResolver nr(pool, diags);
    nr.resolve(m);
    EXPECT_EQ(diags.error_count(), 0u);
    ASSERT_EQ(m.rules.size(), 1u);
    ASSERT_GE(m.rules[0].body.size(), 1u);
    const auto& fp = std::get<FactPattern>(m.rules[0].body[0].data);
    EXPECT_EQ(pool.get(fp.qualifier), "form");
}

TEST(NamespaceImports, ReferConflictErrors) {
    StringPool pool;
    DiagBag diags;
    Module m = parse_src(pool, diags,
        "namespace x.y\n"
        "use form :refer [keyword]\n"
        "use ref :refer [keyword]\n");
    NameResolver nr(pool, diags);
    nr.resolve(m);
    ASSERT_GE(diags.error_count(), 1u);
}
