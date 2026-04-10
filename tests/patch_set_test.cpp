#include <gtest/gtest.h>
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"

class PatchSetTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(PatchSetTest, AddSinglePatch) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("my_mod"), 0);
    auto patches = ps.resolve().get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].field, mora::FieldId::Damage);
    EXPECT_EQ(patches[0].value.as_int(), 25);
}

TEST_F(PatchSetTest, LastWriteWins) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(18), pool.intern("requiem"), 1);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(15), pool.intern("my_mod"), 2);
    auto resolved = ps.resolve();
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].value.as_int(), 15);
}

TEST_F(PatchSetTest, DifferentFieldsNoConflict) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(15), pool.intern("mod_a"), 1);
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Rusty Sword")), pool.intern("mod_b"), 2);
    auto patches = ps.resolve().get_patches_for(0x100);
    EXPECT_EQ(patches.size(), 2u);
}

TEST_F(PatchSetTest, AddKeywordDoesNotConflict) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAAA), pool.intern("mod_a"), 1);
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xBBB), pool.intern("mod_b"), 2);
    auto patches = ps.resolve().get_patches_for(0x100);
    EXPECT_EQ(patches.size(), 2u);
}

TEST_F(PatchSetTest, ConflictReport) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(18), pool.intern("requiem"), 1);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(15), pool.intern("my_mod"), 2);
    auto resolved = ps.resolve();
    auto conflicts = resolved.get_conflicts();
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].target_formid, 0x100u);
    EXPECT_EQ(conflicts[0].field, mora::FieldId::Damage);
    EXPECT_EQ(conflicts[0].entries.size(), 2u);
}

TEST_F(PatchSetTest, SortedByFormID) {
    mora::PatchSet ps;
    ps.add_patch(0x300, mora::FieldId::Damage, mora::FieldOp::Set, mora::Value::make_int(1), pool.intern("mod"), 0);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set, mora::Value::make_int(2), pool.intern("mod"), 0);
    ps.add_patch(0x200, mora::FieldId::Damage, mora::FieldOp::Set, mora::Value::make_int(3), pool.intern("mod"), 0);
    auto all = ps.resolve().all_patches_sorted();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].target_formid, 0x100u);
    EXPECT_EQ(all[1].target_formid, 0x200u);
    EXPECT_EQ(all[2].target_formid, 0x300u);
}

TEST_F(PatchSetTest, TotalPatchCount) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set, mora::Value::make_int(1), pool.intern("mod"), 0);
    ps.add_patch(0x200, mora::FieldId::Name, mora::FieldOp::Set, mora::Value::make_string(pool.intern("x")), pool.intern("mod"), 0);
    EXPECT_EQ(ps.resolve().patch_count(), 2u);
}
