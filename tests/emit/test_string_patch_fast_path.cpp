// Regression for #4: the compiler's fast-path (PatchBuffer → serialize_patch_table)
// previously dropped String-valued patches at the PatchSet → PatchEntry conversion.
// Exercises the public helpers used by the CLI.
#include <gtest/gtest.h>
#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <array>
#include <cstring>

using namespace mora;

namespace {

const emit::SectionDirectoryEntry* find_section(const std::vector<uint8_t>& data,
                                                 emit::SectionId id) {
    emit::PatchFileV2Header h;
    std::memcpy(&h, data.data(), sizeof(h));
    const auto* dir = reinterpret_cast<const emit::SectionDirectoryEntry*>(
        data.data() + sizeof(h));
    for (uint32_t i = 0; i < h.section_count; ++i) {
        if (dir[i].section_id == static_cast<uint32_t>(id)) return &dir[i];
    }
    return nullptr;
}

} // anonymous namespace

TEST(StringPatchFastPath, BuildEntriesEmitsStringIndex) {
    StringPool pool;
    PatchSet ps;
    ps.add_patch(0x100, FieldId::Name, FieldOp::Set,
                 Value::make_string(pool.intern("Nazeem")),
                 pool.intern("test"), 0);
    auto resolved = ps.resolve();

    std::vector<PatchEntry> entries;
    auto string_table = build_patch_entries_and_string_table(resolved, pool, entries);

    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].formid, 0x100u);
    EXPECT_EQ(entries[0].value_type,
              static_cast<uint8_t>(PatchValueType::StringIndex));
    EXPECT_FALSE(string_table.empty());

    // Table bytes: [u16 len][data].
    uint16_t len = 0;
    std::memcpy(&len, string_table.data(), 2);
    EXPECT_EQ(len, 6u);
    std::string recovered(reinterpret_cast<const char*>(string_table.data() + 2), len);
    EXPECT_EQ(recovered, "Nazeem");
    EXPECT_EQ(entries[0].value, 0u); // first (and only) string at offset 0
}

TEST(StringPatchFastPath, SerializerEmitsStringTableSection) {
    StringPool pool;
    PatchSet ps;
    ps.add_patch(0x200, FieldId::Name, FieldOp::Set,
                 Value::make_string(pool.intern("Nazeem")),
                 pool.intern("test"), 0);
    auto resolved = ps.resolve();

    std::vector<PatchEntry> entries;
    auto string_table = build_patch_entries_and_string_table(resolved, pool, entries);

    std::array<uint8_t, 32> zero{};
    std::vector<uint8_t> empty;
    auto bytes = serialize_patch_table(entries, zero, empty, empty, string_table);

    const auto* patches_sec = find_section(bytes, emit::SectionId::Patches);
    const auto* strings_sec = find_section(bytes, emit::SectionId::StringTable);
    ASSERT_NE(patches_sec, nullptr);
    ASSERT_NE(strings_sec, nullptr);
    EXPECT_EQ(patches_sec->size, sizeof(PatchEntry));
    EXPECT_EQ(strings_sec->size, string_table.size());

    const auto* written_entry =
        reinterpret_cast<const PatchEntry*>(bytes.data() + patches_sec->offset);
    EXPECT_EQ(written_entry->value_type,
              static_cast<uint8_t>(PatchValueType::StringIndex));

    const uint8_t* st = bytes.data() + strings_sec->offset;
    uint16_t len = 0;
    std::memcpy(&len, st + written_entry->value, 2);
    EXPECT_EQ(len, 6u);
    std::string recovered(reinterpret_cast<const char*>(st + written_entry->value + 2), len);
    EXPECT_EQ(recovered, "Nazeem");
}
