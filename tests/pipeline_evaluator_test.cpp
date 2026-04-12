#include <gtest/gtest.h>
#include "mora/eval/pipeline_evaluator.h"

using namespace mora;

// ---------------------------------------------------------------------------
// Helper: create a small fact store with NPCs and keywords
// ---------------------------------------------------------------------------

struct TestFixture {
    ChunkPool chunk_pool;
    StringPool string_pool;
    ColumnarFactStore store{chunk_pool};

    // Common FormIDs
    static constexpr uint32_t NPC_A = 0x100;
    static constexpr uint32_t NPC_B = 0x200;
    static constexpr uint32_t NPC_C = 0x300;
    static constexpr uint32_t KW_MAGIC   = 0xA01;
    static constexpr uint32_t KW_WARRIOR = 0xA02;
    static constexpr uint32_t RACE_NORD  = 0xB01;
    static constexpr uint32_t RACE_ELF   = 0xB02;
    static constexpr uint32_t TARGET_KW  = 0xBEEF;
    static constexpr uint32_t TARGET_SP  = 0xCAFE;

    void add_npcs() {
        auto& npcs = store.get_or_create(string_pool.intern("npc"),
                                          {ColType::U32});
        npcs.append_row({NPC_A});
        npcs.append_row({NPC_B});
        npcs.append_row({NPC_C});
    }

    void add_keywords() {
        auto& hk = store.get_or_create(string_pool.intern("has_keyword"),
                                        {ColType::U32, ColType::U32});
        hk.append_row({NPC_A, KW_MAGIC});    // NPC_A has magic keyword
        hk.append_row({NPC_B, KW_MAGIC});    // NPC_B has magic keyword
        hk.append_row({NPC_A, KW_WARRIOR});  // NPC_A also has warrior
        // NPC_C has no keywords
    }

    void add_races() {
        auto& ro = store.get_or_create(string_pool.intern("race_of"),
                                        {ColType::U32, ColType::U32});
        ro.append_row({NPC_A, RACE_NORD});
        ro.append_row({NPC_B, RACE_ELF});
        ro.append_row({NPC_C, RACE_NORD});
    }
};

// ---------------------------------------------------------------------------
// 1. SPID keyword-filtered distribution
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, SpidKeywordFilter) {
    TestFixture f;
    f.add_npcs();
    f.add_keywords();

    auto kw_sid = f.string_pool.intern("keyword");

    // spid_dist: rule 1 distributes keyword TARGET_KW
    auto& dists = f.store.get_or_create(f.string_pool.intern("spid_dist"),
                                         {ColType::U32, ColType::U32, ColType::U32});
    dists.append_row({1, kw_sid.index, TestFixture::TARGET_KW});

    // spid_kw_filter: rule 1 filters by KW_MAGIC
    auto& kwf = f.store.get_or_create(f.string_pool.intern("spid_kw_filter"),
                                       {ColType::U32, ColType::U32});
    kwf.append_row({1, TestFixture::KW_MAGIC});

    f.store.build_all_indexes();

    PatchSet patches;
    evaluate_distributions_columnar(f.store, f.string_pool, patches);

    auto resolved = patches.resolve();
    // NPC_A and NPC_B both have KW_MAGIC -> both get TARGET_KW
    auto& pa = resolved.get_patches_for(TestFixture::NPC_A);
    auto& pb = resolved.get_patches_for(TestFixture::NPC_B);
    auto& pc = resolved.get_patches_for(TestFixture::NPC_C);

    EXPECT_EQ(pa.size(), 1u);
    EXPECT_EQ(pa[0].field, FieldId::Keywords);
    EXPECT_EQ(pa[0].op, FieldOp::Add);
    EXPECT_EQ(pa[0].value.as_formid(), TestFixture::TARGET_KW);

    EXPECT_EQ(pb.size(), 1u);
    EXPECT_EQ(pb[0].value.as_formid(), TestFixture::TARGET_KW);

    EXPECT_TRUE(pc.empty());
}

// ---------------------------------------------------------------------------
// 2. SPID no-filter distribution (all NPCs)
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, SpidNoFilter) {
    TestFixture f;
    f.add_npcs();

    auto spell_sid = f.string_pool.intern("spell");

    auto& dists = f.store.get_or_create(f.string_pool.intern("spid_dist"),
                                         {ColType::U32, ColType::U32, ColType::U32});
    dists.append_row({2, spell_sid.index, TestFixture::TARGET_SP});

    auto& nof = f.store.get_or_create(f.string_pool.intern("spid_no_filter"),
                                       {ColType::U32});
    nof.append_row({2});

    f.store.build_all_indexes();

    PatchSet patches;
    evaluate_distributions_columnar(f.store, f.string_pool, patches);

    auto resolved = patches.resolve();
    // All 3 NPCs get the spell
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A).size(), 1u);
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_B).size(), 1u);
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_C).size(), 1u);

    auto& pa = resolved.get_patches_for(TestFixture::NPC_A);
    EXPECT_EQ(pa[0].field, FieldId::Spells);
    EXPECT_EQ(pa[0].value.as_formid(), TestFixture::TARGET_SP);
}

// ---------------------------------------------------------------------------
// 3. SPID form-filtered distribution (race filter)
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, SpidFormFilter) {
    TestFixture f;
    f.add_npcs();
    f.add_races();

    auto perk_sid = f.string_pool.intern("perk");

    auto& dists = f.store.get_or_create(f.string_pool.intern("spid_dist"),
                                         {ColType::U32, ColType::U32, ColType::U32});
    dists.append_row({3, perk_sid.index, 0xD00D});

    // Filter by RACE_NORD
    auto& ff = f.store.get_or_create(f.string_pool.intern("spid_form_filter"),
                                      {ColType::U32, ColType::U32});
    ff.append_row({3, TestFixture::RACE_NORD});

    f.store.build_all_indexes();

    PatchSet patches;
    evaluate_distributions_columnar(f.store, f.string_pool, patches);

    auto resolved = patches.resolve();
    // NPC_A and NPC_C are Nord; NPC_B is Elf
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A).size(), 1u);
    EXPECT_TRUE(resolved.get_patches_for(TestFixture::NPC_B).empty());
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_C).size(), 1u);

    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A)[0].field, FieldId::Perks);
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A)[0].value.as_formid(), 0xD00Du);
}

// ---------------------------------------------------------------------------
// 4. KID keyword-filtered distribution
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, KidKeywordFilter) {
    TestFixture f;
    f.add_npcs();
    f.add_keywords();

    auto kw_sid = f.string_pool.intern("keyword");

    auto& dists = f.store.get_or_create(f.string_pool.intern("kid_dist"),
                                         {ColType::U32, ColType::U32, ColType::U32});
    dists.append_row({10, kw_sid.index, 0xFACE});

    auto& kwf = f.store.get_or_create(f.string_pool.intern("kid_kw_filter"),
                                       {ColType::U32, ColType::U32});
    kwf.append_row({10, TestFixture::KW_WARRIOR});

    f.store.build_all_indexes();

    PatchSet patches;
    evaluate_distributions_columnar(f.store, f.string_pool, patches);

    auto resolved = patches.resolve();
    // Only NPC_A has KW_WARRIOR
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A).size(), 1u);
    EXPECT_TRUE(resolved.get_patches_for(TestFixture::NPC_B).empty());

    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A)[0].field, FieldId::Keywords);
    EXPECT_EQ(resolved.get_patches_for(TestFixture::NPC_A)[0].value.as_formid(), 0xFACEu);
}

// ---------------------------------------------------------------------------
// 5. Multiple rules, same dist type
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, MultipleRulesSameType) {
    TestFixture f;
    f.add_npcs();
    f.add_keywords();

    auto kw_sid = f.string_pool.intern("keyword");

    auto& dists = f.store.get_or_create(f.string_pool.intern("spid_dist"),
                                         {ColType::U32, ColType::U32, ColType::U32});
    dists.append_row({1, kw_sid.index, 0xAAAA}); // rule 1 -> target 0xAAAA
    dists.append_row({2, kw_sid.index, 0xBBBB}); // rule 2 -> target 0xBBBB

    auto& kwf = f.store.get_or_create(f.string_pool.intern("spid_kw_filter"),
                                       {ColType::U32, ColType::U32});
    kwf.append_row({1, TestFixture::KW_MAGIC});   // rule 1 filters by magic
    kwf.append_row({2, TestFixture::KW_WARRIOR}); // rule 2 filters by warrior

    f.store.build_all_indexes();

    PatchSet patches;
    evaluate_distributions_columnar(f.store, f.string_pool, patches);

    auto resolved = patches.resolve();
    // NPC_A has both KW_MAGIC and KW_WARRIOR -> gets both targets
    // NPC_B has only KW_MAGIC -> gets only 0xAAAA
    auto& pa = resolved.get_patches_for(TestFixture::NPC_A);
    auto& pb = resolved.get_patches_for(TestFixture::NPC_B);

    EXPECT_EQ(pa.size(), 2u);
    EXPECT_EQ(pb.size(), 1u);
    EXPECT_EQ(pb[0].value.as_formid(), 0xAAAAu);
}

// ---------------------------------------------------------------------------
// 6. Empty store produces no patches
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, EmptyStoreNoCrash) {
    ChunkPool chunk_pool;
    StringPool string_pool;
    ColumnarFactStore store(chunk_pool);

    PatchSet patches;
    evaluate_distributions_columnar(store, string_pool, patches);

    auto resolved = patches.resolve();
    EXPECT_EQ(resolved.patch_count(), 0u);
}

// ---------------------------------------------------------------------------
// 7. ColumnarFactStore get_or_create returns same relation on second call
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, FactStoreGetOrCreate) {
    ChunkPool chunk_pool;
    StringPool string_pool;
    ColumnarFactStore store(chunk_pool);

    auto npc_sid = string_pool.intern("npc");
    auto& r1 = store.get_or_create(npc_sid, {ColType::U32});
    r1.append_row({42u});

    auto& r2 = store.get_or_create(npc_sid, {ColType::U32});
    EXPECT_EQ(&r1, &r2);
    EXPECT_EQ(r2.row_count(), 1u);
}

// ---------------------------------------------------------------------------
// 8. ColumnarFactStore get returns nullptr for missing relation
// ---------------------------------------------------------------------------
TEST(PipelineEvaluatorTest, FactStoreGetMissing) {
    ChunkPool chunk_pool;
    StringPool string_pool;
    ColumnarFactStore store(chunk_pool);

    auto missing = string_pool.intern("doesnt_exist");
    EXPECT_EQ(store.get(missing), nullptr);

    const auto& cstore = store;
    EXPECT_EQ(cstore.get(missing), nullptr);
}
