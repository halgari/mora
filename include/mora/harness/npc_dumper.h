#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace mora::harness {

struct NpcData {
    uint32_t formid = 0;
    std::string name;

    // ACBS packed stats
    uint16_t level           = 0;
    uint16_t calc_level_min  = 0;
    uint16_t calc_level_max  = 0;
    uint16_t speed_mult      = 0;

    // ACBS flag bits (kFlags in form_model.h)
    bool essential         = false;
    bool protected_flag    = false;
    bool auto_calc_stats   = false;

    // Form references (dereferenced → FormID at +0x14 of the target form)
    uint32_t race_formid           = 0;
    uint32_t class_formid          = 0;
    uint32_t voice_type_formid     = 0;
    uint32_t skin_formid           = 0;
    uint32_t default_outfit_formid = 0;

    // Polymorphic collection (BGSKeywordForm — same layout as weapon keywords)
    std::vector<uint32_t> keyword_formids;

    // NPC collections (TESSpellList / BGSPerkRankArray / FactionList / ShoutList).
    // Populated via CommonLibSSE-NG typed access when MORA_WITH_COMMONLIB is
    // defined; empty on Linux (synthetic-byte tests don't exercise these).
    std::vector<uint32_t> spell_formids;
    std::vector<uint32_t> perk_formids;
    std::vector<uint32_t> faction_formids;
    std::vector<uint32_t> shout_formids;
};

// Read NPC fields (formid + TESFullName) from a raw form pointer.
// form must point to a TESNPC (FormType 0x2B).
void read_npc_fields(const void* form, NpcData& out);

// Serialize a single NpcData to a JSON line (no trailing newline).
std::string npc_to_jsonl(const NpcData& data);

// Write a vector of NpcData as sorted JSONL. Sorts by formid.
void write_npcs_jsonl(std::vector<NpcData>& npcs, std::ostream& out);

} // namespace mora::harness
