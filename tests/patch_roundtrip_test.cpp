#include <gtest/gtest.h>
#include "mora/emit/patch_writer.h"
#include "mora/emit/patch_reader.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <sstream>

class PatchRoundtripTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(PatchRoundtripTest, EmptyPatchSet) {
    mora::ResolvedPatchSet resolved;
    std::ostringstream out;
    mora::PatchWriter writer(pool);
    writer.write(out, resolved, 0x12345678, 0xABCDEF01);

    std::istringstream in(out.str());
    mora::PatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->load_order_hash, 0x12345678u);
    EXPECT_EQ(result->source_hash, 0xABCDEF01u);
    EXPECT_EQ(result->patches.size(), 0u);
}

TEST_F(PatchRoundtripTest, SinglePatch) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("my_mod"), 0);
    auto resolved = ps.resolve();

    std::ostringstream out;
    mora::PatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    std::istringstream in(out.str());
    mora::PatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->patches.size(), 1u);
    EXPECT_EQ(result->patches[0].target_formid, 0x100u);
    ASSERT_EQ(result->patches[0].fields.size(), 1u);
    EXPECT_EQ(result->patches[0].fields[0].field, mora::FieldId::Damage);
    EXPECT_EQ(result->patches[0].fields[0].value.as_int(), 25);
}

TEST_F(PatchRoundtripTest, MultiplePatches) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("mod"), 0);
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Test Sword")), pool.intern("mod"), 0);
    ps.add_patch(0x200, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAAA), pool.intern("mod"), 0);
    auto resolved = ps.resolve();

    std::ostringstream out;
    mora::PatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    std::istringstream in(out.str());
    mora::PatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->patches.size(), 2u);
}

TEST_F(PatchRoundtripTest, MagicBytesValidation) {
    std::istringstream in("XXXX");
    mora::PatchReader reader(pool);
    auto result = reader.read(in);
    EXPECT_FALSE(result.has_value());
}

TEST_F(PatchRoundtripTest, FloatValue) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(3.14), pool.intern("mod"), 0);
    auto resolved = ps.resolve();

    std::ostringstream out;
    mora::PatchWriter writer(pool);
    writer.write(out, resolved, 0, 0);

    std::istringstream in(out.str());
    mora::PatchReader reader(pool);
    auto result = reader.read(in);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->patches.size(), 1u);
    EXPECT_DOUBLE_EQ(result->patches[0].fields[0].value.as_float(), 3.14);
}
