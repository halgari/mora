#include <gtest/gtest.h>
#include "mora/emit/patch_table.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <cstring>

class PatchTableTest : public ::testing::Test {
protected:
    mora::StringPool pool;

    const mora::PatchTableHeader* get_header(const std::vector<uint8_t>& data) {
        return reinterpret_cast<const mora::PatchTableHeader*>(data.data());
    }

    const mora::PatchEntry* get_entries(const std::vector<uint8_t>& data) {
        auto* hdr = get_header(data);
        return reinterpret_cast<const mora::PatchEntry*>(
            data.data() + sizeof(mora::PatchTableHeader) + hdr->string_table_size);
    }
};

TEST_F(PatchTableTest, EmptyPatchSet) {
    mora::PatchSet ps;
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->magic, 0x4D4F5241u);
    EXPECT_EQ(hdr->version, 3u);
    EXPECT_EQ(hdr->patch_count, 0u);
    EXPECT_EQ(hdr->string_table_size, 0u);
}

TEST_F(PatchTableTest, HeaderMagicAndVersion) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("my_mod"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->magic, 0x4D4F5241u);
    EXPECT_EQ(hdr->version, 3u);
    EXPECT_EQ(hdr->patch_count, 1u);
}

TEST_F(PatchTableTest, FormIDValue) {
    mora::PatchSet ps;
    ps.add_patch(0x200, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAABBCC), pool.intern("mod_a"), 1);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->patch_count, 1u);

    auto* entries = get_entries(data);
    EXPECT_EQ(entries[0].formid, 0x200u);
    EXPECT_EQ(entries[0].field_id, static_cast<uint8_t>(mora::FieldId::Keywords));
    EXPECT_EQ(entries[0].op, static_cast<uint8_t>(mora::FieldOp::Add));
    EXPECT_EQ(entries[0].value_type, static_cast<uint8_t>(mora::PatchValueType::FormID));
    EXPECT_EQ(entries[0].value, 0xAABBCCu);
}

TEST_F(PatchTableTest, IntValue) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::GoldValue, mora::FieldOp::Set,
                 mora::Value::make_int(42), pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* entries = get_entries(data);
    EXPECT_EQ(entries[0].value_type, static_cast<uint8_t>(mora::PatchValueType::Int));
    int64_t val;
    std::memcpy(&val, &entries[0].value, 8);
    EXPECT_EQ(val, 42);
}

TEST_F(PatchTableTest, FloatValue) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(3.14), pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* entries = get_entries(data);
    EXPECT_EQ(entries[0].value_type, static_cast<uint8_t>(mora::PatchValueType::Float));
    double val;
    std::memcpy(&val, &entries[0].value, 8);
    EXPECT_DOUBLE_EQ(val, 3.14);
}

TEST_F(PatchTableTest, StringValue) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Iron Sword")),
                 pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->patch_count, 1u);
    EXPECT_GT(hdr->string_table_size, 0u);

    auto* entries = get_entries(data);
    EXPECT_EQ(entries[0].value_type, static_cast<uint8_t>(mora::PatchValueType::StringIndex));

    // Verify string table contains the string
    const uint8_t* string_table = data.data() + sizeof(mora::PatchTableHeader);
    uint32_t str_offset = static_cast<uint32_t>(entries[0].value);
    uint16_t len;
    std::memcpy(&len, string_table + str_offset, 2);
    EXPECT_EQ(len, 10u); // "Iron Sword" length
    std::string recovered(reinterpret_cast<const char*>(string_table + str_offset + 2), len);
    EXPECT_EQ(recovered, "Iron Sword");
}

TEST_F(PatchTableTest, MixedValueTypes) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("mod_a"), 0);
    ps.add_patch(0x100, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(5.5), pool.intern("mod_a"), 0);
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Great Sword")),
                 pool.intern("mod_a"), 0);
    ps.add_patch(0x100, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xDEAD), pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->patch_count, 4u);

    // All entries should have formid 0x100
    auto* entries = get_entries(data);
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_EQ(entries[i].formid, 0x100u);
    }
}

TEST_F(PatchTableTest, SortedByFormID) {
    mora::PatchSet ps;
    // Add in reverse order
    ps.add_patch(0x300, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(30), pool.intern("mod_a"), 0);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(10), pool.intern("mod_a"), 0);
    ps.add_patch(0x200, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(20), pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->patch_count, 3u);

    auto* entries = get_entries(data);
    EXPECT_EQ(entries[0].formid, 0x100u);
    EXPECT_EQ(entries[1].formid, 0x200u);
    EXPECT_EQ(entries[2].formid, 0x300u);
}

TEST_F(PatchTableTest, HeaderHasNoOffsetFields) {
    // v3 header has no Address Library offsets — CommonLib handles resolution
    EXPECT_EQ(sizeof(mora::PatchTableHeader), 16u);
}

TEST_F(PatchTableTest, StringDeduplication) {
    mora::PatchSet ps;
    auto sword_id = pool.intern("Iron Sword");
    ps.add_patch(0x100, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(sword_id), pool.intern("mod_a"), 0);
    ps.add_patch(0x200, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(sword_id), pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto* hdr = get_header(data);
    EXPECT_EQ(hdr->patch_count, 2u);

    // Both entries should reference the same string table offset
    auto* entries = get_entries(data);
    EXPECT_EQ(entries[0].value, entries[1].value);

    // String table should only contain the string once: 2 bytes length + 10 bytes data
    EXPECT_EQ(hdr->string_table_size, 12u);
}
