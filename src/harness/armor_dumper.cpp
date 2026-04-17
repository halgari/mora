#include "mora/harness/armor_dumper.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace mora::harness {

// Offsets mirror include/mora/data/form_model.h's kArmorSlots +
// kArmorDirectMembers. TESObjectARMO total memory footprint ≈ 0x230.

// TESForm
static constexpr size_t kFormIdOffset        = 0x14;

// Shared components on ARMO (kArmorSlots)
static constexpr size_t kFullNameOffset      = 0x030 + 0x08;
static constexpr size_t kEnchantableOffset   = 0x050 + 0x08;   // formEnchanting
static constexpr size_t kValueOffset         = 0x068 + 0x08;
static constexpr size_t kWeightOffset        = 0x078 + 0x08;
static constexpr size_t kKeywordsArrayOffset = 0x1D8 + 0x08;
static constexpr size_t kKeywordsCountOffset = 0x1D8 + 0x10;

// ArmorDirect absolute
static constexpr size_t kArmorRatingOffset   = 0x200;

static uint32_t deref_formid(const void* form_ptr) {
    if (!form_ptr) return 0;
    uint32_t fid = 0;
    std::memcpy(&fid, static_cast<const char*>(form_ptr) + kFormIdOffset, sizeof(fid));
    return fid;
}

void read_armor_fields(const void* form, ArmorData& out) {
    auto base = static_cast<const char*>(form);

    std::memcpy(&out.formid,       base + kFormIdOffset,      sizeof(out.formid));
    std::memcpy(&out.value,        base + kValueOffset,       sizeof(out.value));
    std::memcpy(&out.weight,       base + kWeightOffset,      sizeof(out.weight));
    std::memcpy(&out.armor_rating, base + kArmorRatingOffset, sizeof(out.armor_rating));

    void* name_ptr = nullptr;
    std::memcpy(static_cast<void*>(&name_ptr), base + kFullNameOffset, sizeof(name_ptr));
    if (name_ptr) {
        out.name = static_cast<const char*>(name_ptr);
    }

    void* ench_ptr = nullptr;
    std::memcpy(static_cast<void*>(&ench_ptr), base + kEnchantableOffset, sizeof(ench_ptr));
    out.enchantment_formid = deref_formid(ench_ptr);

    void* kw_array = nullptr;
    uint32_t kw_count = 0;
    std::memcpy(static_cast<void*>(&kw_array), base + kKeywordsArrayOffset, sizeof(kw_array));
    std::memcpy(&kw_count, base + kKeywordsCountOffset, sizeof(kw_count));

    out.keyword_formids.clear();
    if (kw_array && kw_count > 0) {
        auto** keywords = static_cast<const char**>(kw_array);
        for (uint32_t i = 0; i < kw_count; i++) {
            if (keywords[i]) {
                out.keyword_formids.push_back(deref_formid(keywords[i]));
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
    for (const char c : s) {
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

std::string armor_to_jsonl(const ArmorData& data) {
    std::ostringstream ss;
    ss << "{\"formid\":\"" << format_hex(data.formid) << "\"";
    ss << ",\"name\":\"" << escape_json_string(data.name) << "\"";
    ss << ",\"value\":" << data.value;
    ss << ",\"weight\":" << data.weight;
    ss << ",\"armor_rating\":" << data.armor_rating;
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

void write_armors_jsonl(std::vector<ArmorData>& armors, std::ostream& out) {
    std::sort(armors.begin(), armors.end(),
              [](const ArmorData& a, const ArmorData& b) {
                  return a.formid < b.formid;
              });
    for (const auto& a : armors) {
        out << armor_to_jsonl(a) << "\n";
    }
}

} // namespace mora::harness
