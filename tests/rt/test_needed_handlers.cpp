#include "mora/rt/needed_handlers.h"
#include "mora/dag/graph.h"
#include "mora/dag/opcode.h"
#include "mora/model/relations.h"
#include <gtest/gtest.h>

using namespace mora;
using namespace mora::dag;
using namespace mora::model;

static uint16_t rid_of(std::string_view ns, std::string_view name) {
    for (size_t i = 0; i < kRelationCount; ++i) {
        if (kRelations[i].namespace_ == ns && kRelations[i].name == name)
            return static_cast<uint16_t>(i);
    }
    return static_cast<uint16_t>(-1);
}

TEST(NeededHandlers, OnSinkPullsSinkHandler) {
    DagGraph g;
    DagNode n{};
    n.opcode = DagOpcode::OnSink;
    n.handler_id = HandlerId::PlayerAddGold;
    g.add_node(n);

    auto got = rt::needed_handler_ids(g);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got.count(static_cast<uint16_t>(HandlerId::PlayerAddGold)), 1u);
}

TEST(NeededHandlers, StaticProbeOnFormBringsNoHandlers) {
    // form/* relations are static-source with no read_handler, so a
    // StaticProbe referring to one should contribute nothing.
    uint16_t rid = rid_of("form", "keyword");
    // keyword may exist only if form/keyword is defined; skip if not found.
    if (rid == static_cast<uint16_t>(-1)) GTEST_SKIP();

    DagGraph g;
    DagNode n{};
    n.opcode = DagOpcode::StaticProbe;
    n.relation_id = rid;
    g.add_node(n);

    auto got = rt::needed_handler_ids(g);
    EXPECT_TRUE(got.empty());
}

TEST(NeededHandlers, StateSourceOnRefInCombatAddsReadHandler) {
    uint16_t rid = rid_of("ref", "in_combat");
    ASSERT_NE(rid, static_cast<uint16_t>(-1));

    DagGraph g;
    DagNode n{};
    n.opcode = DagOpcode::StateSource;
    n.relation_id = rid;
    g.add_node(n);

    auto got = rt::needed_handler_ids(g);
    EXPECT_EQ(got.count(static_cast<uint16_t>(HandlerId::RefReadInCombat)), 1u);
}

TEST(NeededHandlers, EventSourceOnEnteredLocationProducesHookName) {
    uint16_t rid = rid_of("event", "entered_location");
    ASSERT_NE(rid, static_cast<uint16_t>(-1));

    DagGraph g;
    DagNode n{};
    n.opcode = DagOpcode::EventSource;
    n.relation_id = rid;
    g.add_node(n);

    auto hooks = rt::needed_hook_names(g);
    EXPECT_EQ(hooks.count("OnLocationChange"), 1u);
}

TEST(NeededHandlers, EmptyGraphProducesEmptySets) {
    DagGraph g;
    EXPECT_TRUE(rt::needed_handler_ids(g).empty());
    EXPECT_TRUE(rt::needed_hook_names(g).empty());
}
