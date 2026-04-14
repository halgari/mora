#include "mora/rt/dag_engine.h"
#include "mora/rt/handler_registry.h"
#include "mora/dag/graph.h"
#include "mora/emit/arrangement_emit.h"
#include <gtest/gtest.h>

using namespace mora::rt;
using namespace mora::dag;
using namespace mora::model;

TEST(DagEngine, EventThroughSourceToOnSinkFiresHandler) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 0});
    auto sink = g.add_node({.opcode = DagOpcode::OnSink,
                            .handler_id = HandlerId::PlayerAddGold});
    g.set_input(sink, 0, src);

    HandlerRegistry reg;
    int calls = 0;
    reg.bind_effect(HandlerId::PlayerAddGold,
        [&](const EffectArgs&){ ++calls; return EffectHandle{1}; });

    DagEngine engine(g, reg);
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);

    // Retractions don't fire for OnSink.
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = -1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);
}

TEST(DagEngine, MaintainSinkCallsApplyThenRetract) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::StateSource, .relation_id = 0});
    auto sink = g.add_node({.opcode = DagOpcode::MaintainSink,
                            .handler_id = HandlerId::RefAddKeyword});
    g.set_input(sink, 0, src);

    HandlerRegistry reg;
    int applies = 0, retracts = 0;
    reg.bind_effect(HandlerId::RefAddKeyword,
        [&](const EffectArgs&){ ++applies; return EffectHandle{42}; });
    reg.bind_retract(HandlerId::RefAddKeyword,
        [&](EffectHandle h){ ++retracts; EXPECT_EQ(h.id, 42u); });

    DagEngine engine(g, reg);
    engine.inject_delta(src, Delta{.tuple = {7u, 9u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(applies, 1);
    EXPECT_EQ(retracts, 0);

    engine.inject_delta(src, Delta{.tuple = {7u, 9u}, .diff = -1});
    engine.run_to_quiescence();
    EXPECT_EQ(retracts, 1);
}

TEST(DagEngine, MultipleBindingsTrackedIndependently) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::StateSource, .relation_id = 0});
    auto sink = g.add_node({.opcode = DagOpcode::MaintainSink,
                            .handler_id = HandlerId::RefAddKeyword});
    g.set_input(sink, 0, src);

    HandlerRegistry reg;
    uint64_t next_handle = 100;
    std::vector<EffectHandle> retracted;
    reg.bind_effect(HandlerId::RefAddKeyword,
        [&](const EffectArgs&){ return EffectHandle{next_handle++}; });
    reg.bind_retract(HandlerId::RefAddKeyword,
        [&](EffectHandle h){ retracted.push_back(h); });

    DagEngine engine(g, reg);
    engine.inject_delta(src, Delta{.tuple = {1u, 1u}, .diff = +1});  // handle=100
    engine.inject_delta(src, Delta{.tuple = {2u, 2u}, .diff = +1});  // handle=101
    engine.run_to_quiescence();

    engine.inject_delta(src, Delta{.tuple = {2u, 2u}, .diff = -1});
    engine.run_to_quiescence();

    ASSERT_EQ(retracted.size(), 1u);
    EXPECT_EQ(retracted[0].id, 101u);
}

TEST(DagEngine, StaticProbeFiltersAndPasses) {
    // Arrangement: Loc=100 exists, Loc=200 does not.
    auto bytes = mora::emit::build_u32_arrangement(
        /*relation_id=*/0,
        std::vector<std::array<uint32_t, 2>>{{{100u, 999u}}},
        /*key_column=*/0);

    DagGraph g;
    auto src   = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 0});
    auto probe = g.add_node({.opcode = DagOpcode::StaticProbe, .relation_id = 1});
    auto sink  = g.add_node({.opcode = DagOpcode::OnSink,
                             .handler_id = HandlerId::PlayerAddGold});
    g.set_input(probe, 0, src);
    g.set_input(sink, 0, probe);

    HandlerRegistry reg;
    int calls = 0;
    reg.bind_effect(HandlerId::PlayerAddGold,
        [&](const EffectArgs&){ ++calls; return EffectHandle{1}; });

    DagEngine engine(g, reg);
    engine.register_arrangement(probe, bytes.data(), bytes.size(), /*key_col_in_delta=*/1);

    // Delta (Player=14, Loc=100) matches; (Player=14, Loc=200) does not.
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);

    engine.inject_delta(src, Delta{.tuple = {14u, 200u}, .diff = +1});
    engine.run_to_quiescence();
    EXPECT_EQ(calls, 1);
}

TEST(DagEngine, StaticProbeWithoutArrangementIsNoOp) {
    DagGraph g;
    auto src   = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 0});
    auto probe = g.add_node({.opcode = DagOpcode::StaticProbe, .relation_id = 1});
    auto sink  = g.add_node({.opcode = DagOpcode::OnSink,
                             .handler_id = HandlerId::PlayerAddGold});
    g.set_input(probe, 0, src);
    g.set_input(sink, 0, probe);

    HandlerRegistry reg;
    int calls = 0;
    reg.bind_effect(HandlerId::PlayerAddGold,
        [&](const EffectArgs&){ ++calls; return EffectHandle{1}; });

    DagEngine engine(g, reg);
    // No register_arrangement call.
    engine.inject_delta(src, Delta{.tuple = {14u, 100u}, .diff = +1});
    engine.run_to_quiescence();
    // Probe without arrangement drops deltas — effect should not fire.
    EXPECT_EQ(calls, 0);
}
