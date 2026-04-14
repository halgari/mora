#include <gtest/gtest.h>
#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <cstring>

class PatchTableTest : public ::testing::Test {
protected:
    mora::StringPool pool;

    mora::emit::PatchFileV2Header get_header(const std::vector<uint8_t>& data) {
        mora::emit::PatchFileV2Header h;
        std::memcpy(&h, data.data(), sizeof(h));
        return h;
    }

    // Locate a section directory entry by id; returns nullptr if not present.
    const mora::emit::SectionDirectoryEntry* find_section(
        const std::vector<uint8_t>& data, mora::emit::SectionId id) {
        auto h = get_header(data);
        const auto* dir = reinterpret_cast<const mora::emit::SectionDirectoryEntry*>(
            data.data() + sizeof(mora::emit::PatchFileV2Header));
        for (uint32_t i = 0; i < h.section_count; ++i) {
            if (dir[i].section_id == static_cast<uint32_t>(id))
                return &dir[i];
        }
        return nullptr;
    }

    const mora::PatchEntry* get_entries(const std::vector<uint8_t>& data) {
        const auto* sec = find_section(data, mora::emit::SectionId::Patches);
        if (!sec) return nullptr;
        return reinterpret_cast<const mora::PatchEntry*>(data.data() + sec->offset);
    }

    uint64_t patch_count(const std::vector<uint8_t>& data) {
        const auto* sec = find_section(data, mora::emit::SectionId::Patches);
        if (!sec) return 0;
        return sec->size / sizeof(mora::PatchEntry);
    }

    const uint8_t* string_table_ptr(const std::vector<uint8_t>& data) {
        const auto* sec = find_section(data, mora::emit::SectionId::StringTable);
        if (!sec) return nullptr;
        return data.data() + sec->offset;
    }

    uint64_t string_table_size(const std::vector<uint8_t>& data) {
        const auto* sec = find_section(data, mora::emit::SectionId::StringTable);
        if (!sec) return 0;
        return sec->size;
    }
};

TEST_F(PatchTableTest, EmptyPatchSet) {
    mora::PatchSet ps;
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto h = get_header(data);
    EXPECT_EQ(h.magic, 0x41524F4Du);
    EXPECT_EQ(h.version, 4u);
    EXPECT_EQ(patch_count(data), 0u);
    EXPECT_EQ(string_table_size(data), 0u);
}

TEST_F(PatchTableTest, HeaderMagicAndVersion) {
    mora::PatchSet ps;
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(25), pool.intern("my_mod"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    auto h = get_header(data);
    EXPECT_EQ(h.magic, 0x41524F4Du);
    EXPECT_EQ(h.version, 4u);
    EXPECT_EQ(patch_count(data), 1u);
}

TEST_F(PatchTableTest, FormIDValue) {
    mora::PatchSet ps;
    ps.add_patch(0x200, mora::FieldId::Keywords, mora::FieldOp::Add,
                 mora::Value::make_formid(0xAABBCC), pool.intern("mod_a"), 1);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    EXPECT_EQ(patch_count(data), 1u);

    auto* entries = get_entries(data);
    ASSERT_NE(entries, nullptr);
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
    ASSERT_NE(entries, nullptr);
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
    ASSERT_NE(entries, nullptr);
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

    EXPECT_EQ(patch_count(data), 1u);
    EXPECT_GT(string_table_size(data), 0u);

    auto* entries = get_entries(data);
    ASSERT_NE(entries, nullptr);
    EXPECT_EQ(entries[0].value_type, static_cast<uint8_t>(mora::PatchValueType::StringIndex));

    const uint8_t* st = string_table_ptr(data);
    ASSERT_NE(st, nullptr);
    uint32_t str_offset = static_cast<uint32_t>(entries[0].value);
    uint16_t len;
    std::memcpy(&len, st + str_offset, 2);
    EXPECT_EQ(len, 10u); // "Iron Sword" length
    std::string recovered(reinterpret_cast<const char*>(st + str_offset + 2), len);
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

    EXPECT_EQ(patch_count(data), 4u);

    auto* entries = get_entries(data);
    ASSERT_NE(entries, nullptr);
    for (uint32_t i = 0; i < 4; i++) {
        EXPECT_EQ(entries[i].formid, 0x100u);
    }
}

TEST_F(PatchTableTest, SortedByFormID) {
    mora::PatchSet ps;
    ps.add_patch(0x300, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(30), pool.intern("mod_a"), 0);
    ps.add_patch(0x100, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(10), pool.intern("mod_a"), 0);
    ps.add_patch(0x200, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_int(20), pool.intern("mod_a"), 0);
    auto resolved = ps.resolve();
    auto data = mora::serialize_patch_table(resolved, pool);

    EXPECT_EQ(patch_count(data), 3u);

    auto* entries = get_entries(data);
    ASSERT_NE(entries, nullptr);
    EXPECT_EQ(entries[0].formid, 0x100u);
    EXPECT_EQ(entries[1].formid, 0x200u);
    EXPECT_EQ(entries[2].formid, 0x300u);
}

TEST_F(PatchTableTest, V2HeaderLayout) {
    // v2 header is 64 bytes; section directory entries are 24 bytes.
    EXPECT_EQ(sizeof(mora::emit::PatchFileV2Header), 64u);
    EXPECT_EQ(sizeof(mora::emit::SectionDirectoryEntry), 24u);
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

    EXPECT_EQ(patch_count(data), 2u);

    auto* entries = get_entries(data);
    ASSERT_NE(entries, nullptr);
    EXPECT_EQ(entries[0].value, entries[1].value);

    // String table should only contain the string once: 2 bytes length + 10 bytes data.
    EXPECT_EQ(string_table_size(data), 12u);
}
