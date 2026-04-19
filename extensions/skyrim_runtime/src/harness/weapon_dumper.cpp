#include "mora/harness/weapon_dumper.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace mora::harness {

// Offsets mirror include/mora/data/form_model.h — same source of truth as
// patch_walker.cpp's typed writes.

// TESForm
static constexpr size_t kFormIdOffset         = 0x14;

// Shared components on WEAP (from kWeaponSlots)
static constexpr size_t kFullNameOffset       = 0x030 + 0x08;
static constexpr size_t kEnchantableOffset    = 0x088 + 0x08; // formEnchanting
static constexpr size_t kValueOffset          = 0x0A0 + 0x08;
static constexpr size_t kWeightOffset         = 0x0B0 + 0x08;
static constexpr size_t kDamageOffset         = 0x0C0 + 0x08;
static constexpr size_t kKeywordsArrayOffset  = 0x140 + 0x08;
static constexpr size_t kKeywordsCountOffset  = 0x140 + 0x10;

// WeaponDirect absolute offsets (kWeaponDirectMembers in form_model.h)
static constexpr size_t kSpeedOffset      = 0x170;
static constexpr size_t kReachOffset      = 0x174;
static constexpr size_t kRangeMinOffset   = 0x178;
static constexpr size_t kRangeMaxOffset   = 0x17C;
static constexpr size_t kStaggerOffset    = 0x188;
static constexpr size_t kCritDamageOffset = 0x1B0;

static uint32_t deref_formid(const void* form_ptr) {
    if (!form_ptr) return 0;
    uint32_t fid = 0;
    std::memcpy(&fid, static_cast<const char*>(form_ptr) + kFormIdOffset, sizeof(fid));
    return fid;
}

void read_weapon_fields(const void* form, WeaponData& out) {
    auto base = static_cast<const char*>(form);

    std::memcpy(&out.formid, base + kFormIdOffset, sizeof(out.formid));
    std::memcpy(&out.damage, base + kDamageOffset, sizeof(out.damage));
    std::memcpy(&out.value,  base + kValueOffset,  sizeof(out.value));
    std::memcpy(&out.weight, base + kWeightOffset, sizeof(out.weight));

    std::memcpy(&out.speed,       base + kSpeedOffset,      sizeof(out.speed));
    std::memcpy(&out.reach,       base + kReachOffset,      sizeof(out.reach));
    std::memcpy(&out.range_min,   base + kRangeMinOffset,   sizeof(out.range_min));
    std::memcpy(&out.range_max,   base + kRangeMaxOffset,   sizeof(out.range_max));
    std::memcpy(&out.stagger,     base + kStaggerOffset,    sizeof(out.stagger));
    std::memcpy(&out.crit_damage, base + kCritDamageOffset, sizeof(out.crit_damage));

    // Name: BSFixedString is a pointer to string data. In test mocks this is
    // nullptr; at runtime we read through the pointer.
    void* name_ptr = nullptr;
    std::memcpy(static_cast<void*>(&name_ptr), base + kFullNameOffset, sizeof(name_ptr));
    if (name_ptr) {
        out.name = static_cast<const char*>(name_ptr);
    }

    // Enchantment: dereferenced TESEnchantableForm.formEnchanting → FormID
    void* ench_ptr = nullptr;
    std::memcpy(static_cast<void*>(&ench_ptr), base + kEnchantableOffset, sizeof(ench_ptr));
    out.enchantment_formid = deref_formid(ench_ptr);

    // Keywords: read pointer to array and count
    void* kw_array = nullptr;
    uint32_t kw_count = 0;
    std::memcpy(static_cast<void*>(&kw_array), base + kKeywordsArrayOffset, sizeof(kw_array));
    std::memcpy(&kw_count, base + kKeywordsCountOffset, sizeof(kw_count));

    out.keyword_formids.clear();
    if (kw_array && kw_count > 0) {
        auto* const* keywords = static_cast<const char**>(kw_array);
        for (uint32_t i = 0; i < kw_count; i++) {
            if (keywords[i]) {
                uint32_t kw_formid{};
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
    for (char const c : s) {
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
    ss << ",\"speed\":" << data.speed;
    ss << ",\"reach\":" << data.reach;
    ss << ",\"range_min\":" << data.range_min;
    ss << ",\"range_max\":" << data.range_max;
    ss << ",\"stagger\":" << data.stagger;
    ss << ",\"crit_damage\":" << data.crit_damage;
    ss << ",\"enchantment\":\"" << format_hex(data.enchantment_formid) << "\"";

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
