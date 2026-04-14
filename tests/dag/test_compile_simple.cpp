#include "mora/dag/compile.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include <gtest/gtest.h>

using namespace mora;
using namespace mora::dag;

static Module parse(StringPool& pool, const char* src) {
    DiagBag d;
    Lexer lex(src, "t.mora", pool, d);
    Parser p(lex, pool, d);
    auto m = p.parse_module();
    NameResolver nr(pool, d);
    nr.resolve(m);
    return m;
}

TEST(DagCompile, SimpleOnRuleProducesSourceAndSink) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\n"
        "on gift(P, L):\n"
        "    event/entered_location(P, L)\n"
        "    => add player/gold(P, 100)\n");

    DagGraph g;
    CompileResult res = compile_dynamic_rules(m, pool, g);
    EXPECT_TRUE(res.success);

    bool has_src = false, has_sink = false;
    for (size_t i = 0; i < g.node_count(); ++i) {
        if (g.node(i).opcode == DagOpcode::EventSource) has_src = true;
        if (g.node(i).opcode == DagOpcode::OnSink)      has_sink = true;
    }
    EXPECT_TRUE(has_src);
    EXPECT_TRUE(has_sink);
}

TEST(DagCompile, StaticRuleIsNotLowered) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\n"
        "r(W):\n"
        "    form/weapon(W)\n"
        "    => set form/damage(W, 20)\n");

    DagGraph g;
    CompileResult res = compile_dynamic_rules(m, pool, g);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(g.node_count(), 0u);
}

TEST(DagCompile, MaintainRuleWithStateSourceAndSink) {
    StringPool pool;
    auto m = parse(pool,
        "namespace x.y\n"
        "maintain danger(R):\n"
        "    ref/base_form(R, Base)\n"
        "    form/faction(Base, @BanditFaction)\n"
        "    => add ref/keyword(R, @DangerMark)\n");

    DagGraph g;
    CompileResult res = compile_dynamic_rules(m, pool, g);
    EXPECT_TRUE(res.success);

    bool has_state = false, has_probe = false, has_maint = false;
    for (size_t i = 0; i < g.node_count(); ++i) {
        auto op = g.node(i).opcode;
        if (op == DagOpcode::StateSource)  has_state = true;
        if (op == DagOpcode::StaticProbe)  has_probe = true;
        if (op == DagOpcode::MaintainSink) has_maint = true;
    }
    EXPECT_TRUE(has_state);
    EXPECT_TRUE(has_probe);
    EXPECT_TRUE(has_maint);
}
