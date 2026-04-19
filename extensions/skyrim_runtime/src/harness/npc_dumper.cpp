#include "mora/harness/npc_dumper.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef MORA_WITH_COMMONLIB
#include <RE/T/TESNPC.h>
#include <RE/T/TESActorBaseData.h>
#include <RE/S/SpellItem.h>
#include <RE/B/BGSPerk.h>
#include <RE/T/TESFaction.h>
#include <RE/T/TESShout.h>
#endif

namespace mora::harness {

// ═══════════════════════════════════════════════════════════════════════════
// Memory layout constants — mirror include/mora/data/form_model.h's
// kNpcDirectMembers, kNpcSlots, kKeywordFormMembers, etc. The same
// offsets are what patch_walker.cpp writes to, so any drift here surfaces
// immediately as a Part 3 integration-test mismatch.
// ═══════════════════════════════════════════════════════════════════════════

// TESForm
static constexpr size_t kFormIdOffset         = 0x14;

// TESActorBaseData (packed @ 0x038)
static constexpr size_t kActorFlagsOffset     = 0x038;
static constexpr size_t kLevelOffset          = 0x040;
static constexpr size_t kCalcLevelMinOffset   = 0x042;
static constexpr size_t kCalcLevelMaxOffset   = 0x044;
static constexpr size_t kSpeedMultOffset      = 0x046;
static constexpr size_t kVoiceTypeOffset      = 0x058;

// TESFullName.fullName on NPC
static constexpr size_t kFullNameOffset       = 0x0D8 + 0x08;

// BGSSkinForm.skin
static constexpr size_t kSkinOffset           = 0x108;

// BGSKeywordForm @ 0x110 — members {keywords @ +0x08, numKeywords @ +0x10}
static constexpr size_t kKeywordsArrayOffset  = 0x110 + 0x08;
static constexpr size_t kKeywordsCountOffset  = 0x110 + 0x10;

// NpcDirect absolute offsets
static constexpr size_t kRaceOffset           = 0x158;
static constexpr size_t kClassOffset          = 0x1C0;
static constexpr size_t kOutfitOffset         = 0x218;

// ACBS flag bits (see form_model.h kFlags).
static constexpr uint32_t kFlagEssential     = 1U << 1;
static constexpr uint32_t kFlagProtected     = 1U << 11;
static constexpr uint32_t kFlagAutoCalcStats = 1U << 4;

// Deref a (possibly null) TESForm pointer and return its FormID, or 0.
static uint32_t deref_formid(const void* form_ptr) {
    if (!form_ptr) return 0;
    uint32_t fid = 0;
    std::memcpy(&fid, static_cast<const char*>(form_ptr) + kFormIdOffset, sizeof(fid));
    return fid;
}

void read_npc_fields(const void* form, NpcData& out) {
    auto base = static_cast<const char*>(form);

    // ── TESForm.formID ─────────────────────────────────────────────────
    std::memcpy(&out.formid, base + kFormIdOffset, sizeof(out.formid));

    // ── ACBS packed stats (uint16 each) ────────────────────────────────
    std::memcpy(&out.level,          base + kLevelOffset,        sizeof(out.level));
    std::memcpy(&out.calc_level_min, base + kCalcLevelMinOffset, sizeof(out.calc_level_min));
    std::memcpy(&out.calc_level_max, base + kCalcLevelMaxOffset, sizeof(out.calc_level_max));
    std::memcpy(&out.speed_mult,     base + kSpeedMultOffset,    sizeof(out.speed_mult));

    // ── ACBS flag bits ─────────────────────────────────────────────────
    uint32_t flags = 0;
    std::memcpy(&flags, base + kActorFlagsOffset, sizeof(flags));
    out.essential       = (flags & kFlagEssential)     != 0;
    out.protected_flag  = (flags & kFlagProtected)     != 0;
    out.auto_calc_stats = (flags & kFlagAutoCalcStats) != 0;

    // ── TESFullName.fullName (BSFixedString — pointer to chars) ────────
    void* name_ptr = nullptr;
    std::memcpy(static_cast<void*>(&name_ptr), base + kFullNameOffset, sizeof(name_ptr));
    if (name_ptr) {
        out.name = static_cast<const char*>(name_ptr);
    }

    // ── Single FormRef fields (deref → read FormID at +0x14) ───────────
    void* race_ptr = nullptr; std::memcpy(static_cast<void*>(&race_ptr),
        base + kRaceOffset, sizeof(race_ptr));
    out.race_formid = deref_formid(race_ptr);

    void* class_ptr = nullptr; std::memcpy(static_cast<void*>(&class_ptr),
        base + kClassOffset, sizeof(class_ptr));
    out.class_formid = deref_formid(class_ptr);

    void* voice_ptr = nullptr; std::memcpy(static_cast<void*>(&voice_ptr),
        base + kVoiceTypeOffset, sizeof(voice_ptr));
    out.voice_type_formid = deref_formid(voice_ptr);

    void* skin_ptr = nullptr; std::memcpy(static_cast<void*>(&skin_ptr),
        base + kSkinOffset, sizeof(skin_ptr));
    out.skin_formid = deref_formid(skin_ptr);

    void* outfit_ptr = nullptr; std::memcpy(static_cast<void*>(&outfit_ptr),
        base + kOutfitOffset, sizeof(outfit_ptr));
    out.default_outfit_formid = deref_formid(outfit_ptr);

    // ── BGSKeywordForm (same layout as weapon keywords) ────────────────
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

    // ── NPC collections: spells, perks, factions, shouts ───────────────
    // Layouts differ by container type (BSTArray vs. raw {ptr, count}) and
    // aren't straightforward to reach via raw memcpy alone. patch_walker
    // uses CommonLibSSE-NG typed access for the same data; do the same
    // here so we read exactly what the runtime writes. Linux builds (no
    // CommonLib) leave these vectors empty.
    out.spell_formids.clear();
    out.perk_formids.clear();
    out.faction_formids.clear();
    out.shout_formids.clear();

#ifdef MORA_WITH_COMMONLIB
    const auto* npc = static_cast<const RE::TESNPC*>(form);

    if (auto* sd = const_cast<RE::TESNPC*>(npc)->GetSpellList()) {
        for (std::uint32_t i = 0; i < sd->numSpells; i++) {
            if (sd->spells && sd->spells[i]) {
                out.spell_formids.push_back(sd->spells[i]->formID);
            }
        }
        for (std::uint32_t i = 0; i < sd->numShouts; i++) {
            if (sd->shouts && sd->shouts[i]) {
                out.shout_formids.push_back(sd->shouts[i]->formID);
            }
        }
    }

    for (std::uint32_t i = 0; i < npc->perkCount; i++) {
        if (npc->perks && npc->perks[i].perk) {
            out.perk_formids.push_back(npc->perks[i].perk->formID);
        }
    }

    for (const auto& fr : npc->factions) {
        if (fr.faction) {
            out.faction_formids.push_back(fr.faction->formID);
        }
    }
#endif
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

static void write_formid_array(std::ostringstream& ss, const char* key,
                               const std::vector<uint32_t>& fids) {
    ss << ",\"" << key << "\":[";
    auto sorted = fids;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << format_hex(sorted[i]) << "\"";
    }
    ss << "]";
}

std::string npc_to_jsonl(const NpcData& data) {
    std::ostringstream ss;
    ss << "{\"formid\":\"" << format_hex(data.formid) << "\"";
    ss << ",\"name\":\"" << escape_json_string(data.name) << "\"";
    ss << ",\"level\":" << data.level;
    ss << ",\"calc_level_min\":" << data.calc_level_min;
    ss << ",\"calc_level_max\":" << data.calc_level_max;
    ss << ",\"speed_mult\":" << data.speed_mult;
    ss << ",\"essential\":" << (data.essential ? "true" : "false");
    ss << ",\"protected\":" << (data.protected_flag ? "true" : "false");
    ss << ",\"auto_calc_stats\":" << (data.auto_calc_stats ? "true" : "false");
    ss << ",\"race\":\"" << format_hex(data.race_formid) << "\"";
    ss << ",\"class\":\"" << format_hex(data.class_formid) << "\"";
    ss << ",\"voice_type\":\"" << format_hex(data.voice_type_formid) << "\"";
    ss << ",\"skin\":\"" << format_hex(data.skin_formid) << "\"";
    ss << ",\"default_outfit\":\"" << format_hex(data.default_outfit_formid) << "\"";

    write_formid_array(ss, "keywords", data.keyword_formids);
    write_formid_array(ss, "spells",   data.spell_formids);
    write_formid_array(ss, "perks",    data.perk_formids);
    write_formid_array(ss, "factions", data.faction_formids);
    write_formid_array(ss, "shouts",   data.shout_formids);

    ss << "}";
    return ss.str();
}

void write_npcs_jsonl(std::vector<NpcData>& npcs, std::ostream& out) {
    std::sort(npcs.begin(), npcs.end(),
              [](const NpcData& a, const NpcData& b) {
                  return a.formid < b.formid;
              });
    for (const auto& n : npcs) {
        out << npc_to_jsonl(n) << "\n";
    }
}

} // namespace mora::harness
