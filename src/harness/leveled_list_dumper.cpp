#include "mora/harness/leveled_list_dumper.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace mora::harness {

// Offsets mirror include/mora/data/form_model.h's kLeveledItemSlots +
// kLeveledListMembers. TESLeveledList component lives at +0x030 on both
// LVLI and LVLN.
//
// We deliberately avoid CommonLibSSE-NG typed access here: in the Windows
// build `TESForm::As<TESLevItem>()` resolves to an undefined symbol for
// the const overload — the template specialization for leveled-list types
// isn't linked through the harness DLL. Raw memcpy sidesteps that; the
// leveled-list in-memory layout for scalars (chanceNone at +0x040) is
// stable across CommonLibSSE versions anyway.
//
// LVLO entries (the pool) have a non-trivial in-memory layout
// (`RE::LEVELED_OBJECT`, 0x18 bytes) that's not worth reproducing in raw
// offsets for the Part 2 MVP — `set_chance_none` is the only setter in
// scope. The `entries` vector stays empty; integration tests for
// `add_leveled_entry` can extend this dumper when that setter is wired in.
static constexpr size_t kFormIdOffset     = 0x14;
static constexpr size_t kLeveledListSlot  = 0x030;
static constexpr size_t kChanceNoneOffset = kLeveledListSlot + 0x10;

void read_leveled_list_fields(const void* form, LeveledListData& out) {
    auto base = static_cast<const char*>(form);

    std::memcpy(&out.formid, base + kFormIdOffset, sizeof(out.formid));
    std::memcpy(&out.chance_none, base + kChanceNoneOffset, sizeof(out.chance_none));

    out.entries.clear();
}

static std::string format_hex(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return ss.str();
}

std::string leveled_list_to_jsonl(const LeveledListData& data) {
    std::ostringstream ss;
    ss << "{\"formid\":\"" << format_hex(data.formid) << "\"";
    ss << ",\"chance_none\":" << static_cast<int>(data.chance_none);
    ss << ",\"entries\":[";
    for (size_t i = 0; i < data.entries.size(); i++) {
        if (i > 0) ss << ",";
        const auto& e = data.entries[i];
        ss << "{\"level\":" << e.level
           << ",\"form\":\"" << format_hex(e.form_formid) << "\""
           << ",\"count\":" << e.count << "}";
    }
    ss << "]}";
    return ss.str();
}

void write_leveled_lists_jsonl(std::vector<LeveledListData>& lists, std::ostream& out) {
    std::sort(lists.begin(), lists.end(),
              [](const LeveledListData& a, const LeveledListData& b) {
                  return a.formid < b.formid;
              });
    for (const auto& ll : lists) {
        out << leveled_list_to_jsonl(ll) << "\n";
    }
}

} // namespace mora::harness
