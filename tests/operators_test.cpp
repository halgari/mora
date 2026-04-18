#include <gtest/gtest.h>
#include "mora/eval/operators.h"

using namespace mora;

// ---------------------------------------------------------------------------
// 1. HashProbeBasic — build relation with 3 rows indexed on col 1.
//    Input chunk with 2 rows. Probe produces correct joined output.
// ---------------------------------------------------------------------------
TEST(OperatorsTest, HashProbeBasic) {
    ChunkPool pool;

    // Build relation: (NPC_FormID: U32, Keyword_FormID: U32)
    ColumnarRelation build_rel(2, {ColType::U32, ColType::U32}, pool);
    build_rel.append_row({0x100, 0xA01});  // NPC_A has keyword A01
    build_rel.append_row({0x200, 0xA01});  // NPC_B has keyword A01
    build_rel.append_row({0x300, 0xA02});  // NPC_C has keyword A02
    build_rel.build_index(1);  // index on keyword col

    // Input chunk: (RuleID, KeywordFormID) — probe on col 1 (keyword)
    ColumnarRelation input_rel(2, {ColType::U32, ColType::U32}, pool);
    input_rel.append_row({1, 0xA01});  // rule 1 filters by A01
    input_rel.append_row({2, 0xA02});  // rule 2 filters by A02

    size_t output_rows = 0;
    std::vector<std::pair<uint32_t, uint32_t>> results; // (rule_id, npc_id)

    scan(input_rel, [&](const DataChunk& input) {
        hash_probe(build_rel, /*probe_col*/1, /*build_key_col*/1,
                   input, pool, [&](DataChunk& joined) {
            // joined: (RuleID:0, KeywordFormID:1, NPC_FormID:2, KW_FormID:3)
            for (size_t i = 0; i < joined.sel.count; i++) {
                auto row = joined.sel.indices[i];
                uint32_t rule_id = joined.u32(0)->data[row];
                uint32_t npc_id = joined.u32(2)->data[row];
                results.push_back({rule_id, npc_id});
                output_rows++;
            }
        });
    });

    // Rule 1 (A01) matches NPC_A and NPC_B = 2 rows
    // Rule 2 (A02) matches NPC_C = 1 row
    EXPECT_EQ(output_rows, 3u);

    // Check specific matches exist
    bool found_r1_a = false, found_r1_b = false, found_r2_c = false;
    for (auto& [rule, npc] : results) {
        if (rule == 1 && npc == 0x100) found_r1_a = true;
        if (rule == 1 && npc == 0x200) found_r1_b = true;
        if (rule == 2 && npc == 0x300) found_r2_c = true;
    }
    EXPECT_TRUE(found_r1_a);
    EXPECT_TRUE(found_r1_b);
    EXPECT_TRUE(found_r2_c);
}

// ---------------------------------------------------------------------------
// 2. SemiJoinBasic — input with 5 rows, semi-join against a 3-row relation.
//    Only matching rows survive.
// ---------------------------------------------------------------------------
TEST(OperatorsTest, SemiJoinBasic) {
    ChunkPool pool;

    // NPC relation: (FormID)
    ColumnarRelation npc_rel(1, {ColType::U32}, pool);
    npc_rel.append_row({0x100});
    npc_rel.append_row({0x200});
    npc_rel.append_row({0x300});
    npc_rel.build_index(0);

    // Input: 5 rows with NPC FormIDs in col 0, some exist in npc_rel, some don't
    ColumnarRelation input_rel(2, {ColType::U32, ColType::U32}, pool);
    input_rel.append_row({0x100, 1});  // exists
    input_rel.append_row({0x999, 2});  // does not exist
    input_rel.append_row({0x200, 3});  // exists
    input_rel.append_row({0x888, 4});  // does not exist
    input_rel.append_row({0x300, 5});  // exists

    size_t surviving = 0;
    scan(input_rel, [&](DataChunk input) {  // copy so we can mutate sel
        semi_join(npc_rel, /*key_col*/0, /*input_col*/0,
                  input, [&](DataChunk& filtered) {
            surviving += filtered.sel.count;
        });
    });

    EXPECT_EQ(surviving, 3u);
}

// ---------------------------------------------------------------------------
// 6. HashProbeNoMatch — probe values that don't exist in build relation
// ---------------------------------------------------------------------------
TEST(OperatorsTest, HashProbeNoMatch) {
    ChunkPool pool;

    ColumnarRelation build_rel(2, {ColType::U32, ColType::U32}, pool);
    build_rel.append_row({0x100, 0xA01});
    build_rel.build_index(1);

    ColumnarRelation input_rel(2, {ColType::U32, ColType::U32}, pool);
    input_rel.append_row({1, 0xFFF});  // no match

    size_t output_rows = 0;
    scan(input_rel, [&](const DataChunk& input) {
        hash_probe(build_rel, 1, 1, input, pool, [&](DataChunk& joined) {
            output_rows += joined.sel.count;
        });
    });

    EXPECT_EQ(output_rows, 0u);
}

// ---------------------------------------------------------------------------
// 7. SemiJoinAllPass — all input rows exist in the lookup relation
// ---------------------------------------------------------------------------
TEST(OperatorsTest, SemiJoinAllPass) {
    ChunkPool pool;

    ColumnarRelation lookup_rel(1, {ColType::U32}, pool);
    lookup_rel.append_row({10});
    lookup_rel.append_row({20});
    lookup_rel.append_row({30});
    lookup_rel.build_index(0);

    ColumnarRelation input_rel(1, {ColType::U32}, pool);
    input_rel.append_row({10});
    input_rel.append_row({20});
    input_rel.append_row({30});

    size_t surviving = 0;
    scan(input_rel, [&](DataChunk input) {
        semi_join(lookup_rel, 0, 0, input, [&](DataChunk& filtered) {
            surviving += filtered.sel.count;
        });
    });

    EXPECT_EQ(surviving, 3u);
}

// ---------------------------------------------------------------------------
// 8. SemiJoinNonePass — no input rows exist in the lookup relation
// ---------------------------------------------------------------------------
TEST(OperatorsTest, SemiJoinNonePass) {
    ChunkPool pool;

    ColumnarRelation lookup_rel(1, {ColType::U32}, pool);
    lookup_rel.append_row({10});
    lookup_rel.build_index(0);

    ColumnarRelation input_rel(1, {ColType::U32}, pool);
    input_rel.append_row({99});
    input_rel.append_row({88});

    int calls = 0;
    scan(input_rel, [&](DataChunk input) {
        semi_join(lookup_rel, 0, 0, input, [&](DataChunk&) {
            calls++;
        });
    });

    EXPECT_EQ(calls, 0);  // sink never called when nothing survives
}
