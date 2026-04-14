#include "mora/dag/graph.h"
#include <gtest/gtest.h>

using namespace mora::dag;

TEST(DagGraph, AddNodeAssignsSequentialIds) {
    DagGraph g;
    auto a = g.add_node({.opcode = DagOpcode::EventSource, .relation_id = 1});
    auto b = g.add_node({.opcode = DagOpcode::Filter});
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 1u);
    EXPECT_EQ(g.node_count(), 2u);
}

TEST(DagGraph, ConnectInput) {
    DagGraph g;
    auto src = g.add_node({.opcode = DagOpcode::EventSource});
    auto flt = g.add_node({.opcode = DagOpcode::Filter});
    g.set_input(flt, 0, src);
    EXPECT_EQ(g.node(flt).inputs[0], src);
    EXPECT_EQ(g.node(flt).input_count, 1u);
}

TEST(DagGraph, TopologicalOrderIsInsertionOrderWhenLinear) {
    DagGraph g;
    auto a = g.add_node({.opcode = DagOpcode::EventSource});
    auto b = g.add_node({.opcode = DagOpcode::Filter});
    g.set_input(b, 0, a);
    auto order = g.topological_order();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
}

TEST(DagGraph, NodesAccessor) {
    DagGraph g;
    g.add_node({.opcode = DagOpcode::EventSource});
    EXPECT_EQ(g.nodes().size(), 1u);
}
