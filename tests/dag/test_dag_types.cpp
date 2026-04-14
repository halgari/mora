#include "mora/dag/node.h"
#include "mora/dag/opcode.h"
#include <gtest/gtest.h>

using namespace mora::dag;

TEST(DagTypes, OpcodeEnumDistinct) {
    EXPECT_NE(DagOpcode::Filter, DagOpcode::Project);
    EXPECT_NE(DagOpcode::StaticProbe, DagOpcode::HashJoin);
    EXPECT_NE(DagOpcode::MaintainSink, DagOpcode::OnSink);
}

TEST(DagTypes, DagNodeDefaults) {
    DagNode n;
    EXPECT_EQ(n.opcode, DagOpcode::Unknown);
    EXPECT_EQ(n.input_count, 0u);
}

TEST(DagTypes, MaxInputsCompileTimeCheck) {
    static_assert(kMaxDagInputs >= 2);
    SUCCEED();
}

TEST(DagTypes, DagNodeFitsInReasonableSize) {
    static_assert(sizeof(DagNode) <= 64);
    SUCCEED();
}
