#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mora::harness {

struct ArmorData {
    uint32_t formid = 0;
    std::string name;

    // Shared components
    int32_t value  = 0;
    float   weight = 0.0f;

    // ArmorDirect
    uint32_t armor_rating      = 0;
    uint32_t enchantment_formid = 0;

    // BGSKeywordForm
    std::vector<uint32_t> keyword_formids;
};

// Read armor fields from a raw form pointer.
// form must point to a TESObjectARMO (FormType 0x1A).
void read_armor_fields(const void* form, ArmorData& out);

std::string armor_to_jsonl(const ArmorData& data);

// Sort by formid and write one JSONL line per armor.
void write_armors_jsonl(std::vector<ArmorData>& armors, std::ostream& out);

} // namespace mora::harness
