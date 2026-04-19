#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mora::harness {

struct LeveledEntry {
    uint16_t level = 0;
    uint32_t form_formid = 0;
    uint16_t count = 0;
};

struct LeveledListData {
    uint32_t formid = 0;
    int8_t   chance_none = 0;  // LVLD percent 0..100
    std::vector<LeveledEntry> entries;
};

// Read leveled list fields from a raw form pointer.
// form must point to a TESLevItem (0x2D) or TESLevCharacter (0x2C) —
// both share TESLeveledList's layout.
void read_leveled_list_fields(const void* form, LeveledListData& out);

std::string leveled_list_to_jsonl(const LeveledListData& data);

void write_leveled_lists_jsonl(std::vector<LeveledListData>& lists, std::ostream& out);

} // namespace mora::harness
