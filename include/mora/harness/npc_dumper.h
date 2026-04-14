#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mora::harness {

struct NpcData {
    uint32_t formid = 0;
    std::string name;
};

// Read NPC fields (formid + TESFullName) from a raw form pointer.
// form must point to a TESNPC (FormType 0x2B).
void read_npc_fields(const void* form, NpcData& out);

// Serialize a single NpcData to a JSON line (no trailing newline).
std::string npc_to_jsonl(const NpcData& data);

// Write a vector of NpcData as sorted JSONL. Sorts by formid.
void write_npcs_jsonl(std::vector<NpcData>& npcs, std::ostream& out);

} // namespace mora::harness
