#include <gtest/gtest.h>
#include "mora/eval/operators.h"
#include "mora/eval/patch_buffer.h"
#include "mora/eval/pipeline_evaluator.h"
#include "mora/emit/patch_table.h"

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
// 3. PatchBufferSortDedup — add 100 entries with duplicates, sort_and_dedup,
//    verify correct count and ordering.
// ---------------------------------------------------------------------------
TEST(OperatorsTest, PatchBufferSortDedup) {
    PatchBuffer buf;
    uint8_t add_op = static_cast<uint8_t>(FieldOp::Add);

    // Add 100 entries: 50 unique, 50 duplicates
    for (uint32_t i = 0; i < 50; i++) {
        buf.emit(i, 6, add_op, 0, i * 10);
        buf.emit(i, 6, add_op, 0, i * 10);  // duplicate
    }

    ASSERT_EQ(buf.size(), 100u);
    buf.sort_and_dedup();
    ASSERT_EQ(buf.size(), 50u);

    // Verify sorted
    for (size_t i = 1; i < buf.size(); i++) {
        EXPECT_LE(buf.entries()[i-1].formid, buf.entries()[i].formid);
    }
}

// ---------------------------------------------------------------------------
// 4. PatchBufferEmpty — sort_and_dedup on empty buffer, no crash.
// ---------------------------------------------------------------------------
TEST(OperatorsTest, PatchBufferEmpty) {
    PatchBuffer buf;
    buf.sort_and_dedup();
    EXPECT_EQ(buf.size(), 0u);
}

// ---------------------------------------------------------------------------
// 5. FullPipelineIntegration — set up SPID distribution with columnar store,
//    run through operator pipeline, verify correct patches in PatchBuffer.
// ---------------------------------------------------------------------------
TEST(OperatorsTest, FullPipelineIntegration) {
    ChunkPool chunk_pool;
    StringPool string_pool;
    ColumnarFactStore store(chunk_pool);

    constexpr uint32_t NPC_A = 0x100;
    constexpr uint32_t NPC_B = 0x200;
    constexpr uint32_t NPC_C = 0x300;
    constexpr uint32_t KW_MAGIC = 0xA01;
    constexpr uint32_t TARGET_KW = 0xBEEF;

    // Set up NPC relation
    auto& npcs = store.get_or_create(string_pool.intern("npc"), {ColType::U32});
    npcs.append_row({NPC_A});
    npcs.append_row({NPC_B});
    npcs.append_row({NPC_C});

    // Set up has_keyword relation
    auto& hk = store.get_or_create(string_pool.intern("has_keyword"),
                                    {ColType::U32, ColType::U32});
    hk.append_row({NPC_A, KW_MAGIC});
    hk.append_row({NPC_B, KW_MAGIC});

    auto kw_sid = string_pool.intern("keyword");

    // Set up spid_dist: rule 1 distributes keyword TARGET_KW
    auto& dists = store.get_or_create(string_pool.intern("spid_dist"),
                                       {ColType::U32, ColType::U32, ColType::U32});
    dists.append_row({1, kw_sid.index, TARGET_KW});

    // Set up spid_kw_filter: rule 1 filters by KW_MAGIC
    auto& kwf = store.get_or_create(string_pool.intern("spid_kw_filter"),
                                     {ColType::U32, ColType::U32});
    kwf.append_row({1, KW_MAGIC});

    store.build_all_indexes();

    // Evaluate using PatchBuffer path
    PatchBuffer patch_buf;
    evaluate_distributions_columnar(store, string_pool, patch_buf);
    patch_buf.sort_and_dedup();

    // NPC_A and NPC_B both have KW_MAGIC -> both get TARGET_KW
    // NPC_C has no keywords -> no patches
    EXPECT_EQ(patch_buf.size(), 2u);

    // Verify entries
    bool found_a = false, found_b = false;
    for (const auto& e : patch_buf.entries()) {
        EXPECT_EQ(e.field_id, static_cast<uint8_t>(FieldId::Keywords));
        EXPECT_EQ(e.op, static_cast<uint8_t>(FieldOp::Add));
        EXPECT_EQ(e.value, TARGET_KW);
        if (e.formid == NPC_A) found_a = true;
        if (e.formid == NPC_B) found_b = true;
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
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
