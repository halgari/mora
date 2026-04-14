// End-to-end dynamic-rule pipeline test:
//   parse → resolve → compile DAG → serialize → deserialize
//   → stand up engine with stub handler → inject event → assert fire.

#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/dag/compile.h"
#include "mora/dag/bytecode.h"
#include "mora/rt/dag_engine.h"
#include "mora/rt/handler_registry.h"

#include <gtest/gtest.h>

using namespace mora;

TEST(DynamicEndToEnd, OnRuleFiresHandlerForMatchingEvent) {
    // ── Parse + resolve ─────────────────────────────────────────────────
    StringPool pool;
    DiagBag diag;
    const char* src =
        "namespace x.y\n"
        "on greet(P, L):\n"
        "    event/entered_location(P, L)\n"
        "    => add player/gold(P, 100)\n";
    Lexer lex(src, "t.mora", pool, diag);
    Parser p(lex, pool, diag);
    auto m = p.parse_module();
    NameResolver nr(pool, diag);
    nr.resolve(m);
    ASSERT_EQ(diag.error_count(), 0u);

    // ── Compile to DAG ──────────────────────────────────────────────────
    dag::DagGraph g;
    auto cr = dag::compile_dynamic_rules(m, pool, g);
    ASSERT_TRUE(cr.success);
    ASSERT_GT(g.node_count(), 0u);

    // ── Round-trip through bytecode ─────────────────────────────────────
    auto bytes = dag::serialize_dag(g);
    auto loaded = dag::deserialize_dag(bytes.data(), bytes.size());
    ASSERT_EQ(loaded.node_count(), g.node_count());

    // ── Stand up engine with a stub handler ─────────────────────────────
    rt::HandlerRegistry reg;
    int fires = 0;
    reg.bind_effect(model::HandlerId::PlayerAddGold,
        [&](const rt::EffectArgs&){ ++fires; return rt::EffectHandle{1}; });

    rt::DagEngine engine(loaded, reg);

    // Find the EventSource node in the deserialized graph.
    uint32_t src_node = static_cast<uint32_t>(-1);
    for (uint32_t i = 0; i < loaded.node_count(); ++i) {
        if (loaded.node(i).opcode == dag::DagOpcode::EventSource) {
            src_node = i;
            break;
        }
    }
    ASSERT_NE(src_node, static_cast<uint32_t>(-1));

    // ── Inject an event, run, assert handler fired exactly once ─────────
    engine.inject_delta(src_node, rt::Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(fires, 1);
}
