#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mora::harness {

struct WeaponData {
    uint32_t formid = 0;
    std::string name;
    uint16_t damage = 0;
    int32_t value = 0;
    float weight = 0.0f;
    std::vector<uint32_t> keyword_formids;
};

// Read weapon fields from a raw form pointer (Skyrim memory layout).
// form must point to the start of a TESObjectWEAP (0x220 bytes).
void read_weapon_fields(const void* form, WeaponData& out);

// Serialize a single WeaponData to a JSON line (no trailing newline).
std::string weapon_to_jsonl(const WeaponData& data);

// Write a vector of WeaponData as sorted JSONL to an output stream.
// Sorts by formid before writing.
void write_weapons_jsonl(std::vector<WeaponData>& weapons, std::ostream& out);

} // namespace mora::harness
