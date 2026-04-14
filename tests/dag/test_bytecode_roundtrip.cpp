#include "mora/dag/bytecode.h"
#include "mora/dag/graph.h"
#include <gtest/gtest.h>

using namespace mora::dag;

TEST(DagBytecode, RoundTripSingleNode) {
    DagGraph g;
    g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 7});
    auto bytes = serialize_dag(g);
    auto loaded = deserialize_dag(bytes.data(), bytes.size());
    ASSERT_EQ(loaded.node_count(), 1u);
    EXPECT_EQ(loaded.node(0).opcode, DagOpcode::EventSource);
    EXPECT_EQ(loaded.node(0).relation_id, 7u);
}

TEST(DagBytecode, RoundTripMultipleNodesWithInputs) {
    DagGraph g;
    auto a = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 1});
    auto b = g.add_node({.opcode = DagOpcode::Filter});
    g.set_input(b, 0, a);
    auto c = g.add_node({.opcode = DagOpcode::OnSink,
                         .handler_id = mora::model::HandlerId::PlayerAddGold});
    g.set_input(c, 0, b);

    auto bytes = serialize_dag(g);
    auto loaded = deserialize_dag(bytes.data(), bytes.size());
    EXPECT_EQ(loaded.node_count(), 3u);
    EXPECT_EQ(loaded.node(c).inputs[0], b);
    EXPECT_EQ(loaded.node(c).handler_id, mora::model::HandlerId::PlayerAddGold);
}

TEST(DagBytecode, BadMagicReturnsEmptyGraph) {
    uint8_t bogus[16] = {0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0};
    auto loaded = deserialize_dag(bogus, sizeof(bogus));
    EXPECT_EQ(loaded.node_count(), 0u);
}

TEST(DagBytecode, EmptyGraphRoundTrips) {
    DagGraph g;
    auto bytes = serialize_dag(g);
    auto loaded = deserialize_dag(bytes.data(), bytes.size());
    EXPECT_EQ(loaded.node_count(), 0u);
}
