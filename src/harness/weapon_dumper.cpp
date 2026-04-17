#include "mora/harness/weapon_dumper.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace mora::harness {

// Offsets from skyrim_abi.h weapon_offsets
static constexpr size_t kFormIdOffset      = 0x14;
static constexpr size_t kFullNameOffset    = 0x030 + 0x08; // TESFullName.fullName
static constexpr size_t kValueOffset       = 0x0A0 + 0x08; // TESValueForm.value
static constexpr size_t kWeightOffset      = 0x0B0 + 0x08; // TESWeightForm.weight
static constexpr size_t kDamageOffset      = 0x0C0 + 0x08; // TESAttackDamageForm.attackDamage
static constexpr size_t kKeywordsArrayOffset = 0x140 + 0x08; // BGSKeywordForm.keywords
static constexpr size_t kKeywordsCountOffset = 0x140 + 0x10; // BGSKeywordForm.numKeywords

void read_weapon_fields(const void* form, WeaponData& out) {
    auto base = static_cast<const char*>(form);

    std::memcpy(&out.formid, base + kFormIdOffset, sizeof(out.formid));
    std::memcpy(&out.damage, base + kDamageOffset, sizeof(out.damage));
    std::memcpy(&out.value,  base + kValueOffset,  sizeof(out.value));
    std::memcpy(&out.weight, base + kWeightOffset, sizeof(out.weight));

    // Name: BSFixedString is a pointer to string data. In test mocks this is
    // nullptr; at runtime we read through the pointer.
    void* name_ptr = nullptr;
    std::memcpy(static_cast<void*>(&name_ptr), base + kFullNameOffset, sizeof(name_ptr));
    if (name_ptr) {
        out.name = static_cast<const char*>(name_ptr);
    }

    // Keywords: read pointer to array and count
    void* kw_array = nullptr;
    uint32_t kw_count = 0;
    std::memcpy(static_cast<void*>(&kw_array), base + kKeywordsArrayOffset, sizeof(kw_array));
    std::memcpy(&kw_count, base + kKeywordsCountOffset, sizeof(kw_count));

    out.keyword_formids.clear();
    if (kw_array && kw_count > 0) {
        auto** keywords = static_cast<const char**>(kw_array);
        for (uint32_t i = 0; i < kw_count; i++) {
            if (keywords[i]) {
                uint32_t kw_formid;
                std::memcpy(&kw_formid, keywords[i] + 0x14, sizeof(kw_formid));
                out.keyword_formids.push_back(kw_formid);
            }
        }
    }
}

static std::string format_hex(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return ss.str();
}

static std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string weapon_to_jsonl(const WeaponData& data) {
    std::ostringstream ss;
    ss << "{\"formid\":\"" << format_hex(data.formid) << "\"";
    ss << ",\"name\":\"" << escape_json_string(data.name) << "\"";
    ss << ",\"damage\":" << data.damage;
    ss << ",\"value\":" << data.value;
    ss << ",\"weight\":" << data.weight;

    ss << ",\"keywords\":[";
    auto sorted_kw = data.keyword_formids;
    std::sort(sorted_kw.begin(), sorted_kw.end());
    for (size_t i = 0; i < sorted_kw.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << format_hex(sorted_kw[i]) << "\"";
    }
    ss << "]}";

    return ss.str();
}

void write_weapons_jsonl(std::vector<WeaponData>& weapons, std::ostream& out) {
    std::sort(weapons.begin(), weapons.end(),
              [](const WeaponData& a, const WeaponData& b) {
                  return a.formid < b.formid;
              });

    for (const auto& w : weapons) {
        out << weapon_to_jsonl(w) << "\n";
    }
}

} // namespace mora::harness
