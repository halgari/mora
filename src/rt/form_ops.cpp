#include "mora/rt/form_ops.h"
#include "mora/data/form_constants.h"
#include <cstring>

using namespace mora::form_type;
using namespace mora;

namespace {

// FieldId values (from patch_set.h FieldId enum)
constexpr uint16_t kDamage      = 2;
constexpr uint16_t kArmorRating = 3;
constexpr uint16_t kGoldValue   = 4;
constexpr uint16_t kWeight      = 5;

// BGSKeywordForm layout (shared across NPC/Weapon/Armor — offset varies by form type)
//   +0x00: vtable
//   +0x08: BGSKeyword** keywords
//   +0x10: uint32_t  numKeywords
constexpr uint64_t kKwArrayOff = 0x08;
constexpr uint64_t kKwCountOff = 0x10;

} // anonymous namespace

namespace mora::rt {

uint64_t get_field_offset(uint8_t ft, uint16_t field_id) {
    if (ft == form_type::kWeapon) {
        switch (field_id) {
            case kDamage:    return weapon_layout::kAttackDamage + kComponentMember;  // uint16_t
            case kGoldValue: return weapon_layout::kValueForm    + kComponentMember;  // int32_t
            case kWeight:    return weapon_layout::kWeightForm   + kComponentMember;  // float
            default:         return 0;
        }
    }
    if (ft == form_type::kArmor) {
        switch (field_id) {
            case kArmorRating: return armor_layout::kArmorRating;                      // uint32_t
            case kGoldValue:   return armor_layout::kValueForm   + kComponentMember;   // int32_t
            case kWeight:      return armor_layout::kWeightForm  + kComponentMember;   // float
            default:           return 0;
        }
    }
    return 0;
}

// ── Per-form-type component offsets ─────────────────────────────────

static uint64_t get_keyword_form_offset(uint8_t ft) {
    switch (ft) {
        case form_type::kNPC:    return npc_layout::kKeywordForm;
        case form_type::kWeapon: return weapon_layout::kKeywordForm;
        case form_type::kArmor:  return armor_layout::kKeywordForm;
        default:                 return 0;
    }
}

static uint64_t get_full_name_offset(uint8_t ft) {
    switch (ft) {
        case form_type::kNPC:    return npc_layout::kFullName;
        case form_type::kWeapon: return weapon_layout::kFullName;
        case form_type::kArmor:  return armor_layout::kFullName;
        default:                 return 0;
    }
}

// ── MemoryManager helper ────────────────────────────────────────────
// Resolves the three MemoryManager function pointers from offsets and
// returns the singleton. Centralizes the setup so the add/remove
// helpers can focus on array manipulation.

struct MemMgrFns {
    void* mgr;
    MemMgr_Allocate_t allocate;
    MemMgr_Deallocate_t deallocate;
};

static bool resolve_memmgr(void* skyrim_base,
                            uint64_t singleton_off,
                            uint64_t allocate_off,
                            uint64_t deallocate_off,
                            MemMgrFns& out) {
    auto get_singleton = reinterpret_cast<MemMgr_GetSingleton_t>(
        reinterpret_cast<char*>(skyrim_base) + singleton_off);
    out.allocate = reinterpret_cast<MemMgr_Allocate_t>(
        reinterpret_cast<char*>(skyrim_base) + allocate_off);
    out.deallocate = reinterpret_cast<MemMgr_Deallocate_t>(
        reinterpret_cast<char*>(skyrim_base) + deallocate_off);
    out.mgr = get_singleton();
    return out.mgr != nullptr;
}

// Reallocate a pointer array: copies old contents, returns new buffer.
// Caller must free the old buffer via mm.deallocate. Returns nullptr on OOM.
static void** realloc_ptr_array(const MemMgrFns& mm, void** old_data,
                                 uint32_t old_count, uint32_t new_count) {
    void** fresh = reinterpret_cast<void**>(
        mm.allocate(mm.mgr, new_count * sizeof(void*), 0, false));
    if (!fresh) return nullptr;
    if (old_data && old_count > 0) {
        std::memcpy(fresh, old_data, old_count * sizeof(void*));
    }
    return fresh;
}

} // close mora::rt namespace

// ── Name (BSFixedString) write ──────────────────────────────────────

extern "C" void mora_rt_write_name(void* skyrim_base, void* form, const char* name,
                                    uint64_t ctor8_offset, uint64_t release8_offset) {
    using namespace mora::rt;
    if (!form || !name) return;

    uint8_t ft = get_form_type(form);
    uint64_t fn_offset = get_full_name_offset(ft);
    if (fn_offset == 0) return;

    // TESFullName.fullName (BSFixedString) is at component + 0x08
    char** name_ptr = reinterpret_cast<char**>(
        reinterpret_cast<char*>(form) + fn_offset + 0x08);

    auto ctor8 = reinterpret_cast<BSFixedString_ctor8_t>(
        reinterpret_cast<char*>(skyrim_base) + ctor8_offset);
    auto release8 = reinterpret_cast<BSFixedString_release8_t>(
        reinterpret_cast<char*>(skyrim_base) + release8_offset);

    // Release old string (decrement refcount)
    const char* old_name = *name_ptr;
    if (old_name) {
        release8(&old_name);
    }

    // Intern new string via ctor8. Takes BSFixedString* (which is just a char**)
    // and a const char*, and sets *self = interned pointer.
    ctor8(name_ptr, name);
}

// ── Keyword add (NPC / Weapon / Armor) ──────────────────────────────

extern "C" void mora_rt_add_keyword(void* skyrim_base, void* form, void* keyword_form,
                                     uint64_t singleton_off,
                                     uint64_t allocate_off,
                                     uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !keyword_form) return;

    uint8_t ft = get_form_type(form);
    uint64_t kf_offset = get_keyword_form_offset(ft);
    if (kf_offset == 0) return;

    char* kf_base = reinterpret_cast<char*>(form) + kf_offset;
    void*** kw_array_slot = reinterpret_cast<void***>(kf_base + kKwArrayOff);
    uint32_t* num_kw_ptr  = reinterpret_cast<uint32_t*>(kf_base + kKwCountOff);

    void** old_keywords = *kw_array_slot;
    uint32_t old_count = *num_kw_ptr;

    // Already present? Idempotent add.
    for (uint32_t i = 0; i < old_count; i++) {
        if (old_keywords[i] == keyword_form) return;
    }

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
        return;
    }

    uint32_t new_count = old_count + 1;
    void** fresh = realloc_ptr_array(mm, old_keywords, old_count, new_count);
    if (!fresh) return;

    fresh[old_count] = keyword_form;

    *kw_array_slot = fresh;
    *num_kw_ptr = new_count;

    if (old_keywords) {
        mm.deallocate(mm.mgr, old_keywords, false);
    }
}

// ── Keyword remove (NPC / Weapon / Armor) ───────────────────────────

extern "C" void mora_rt_remove_keyword(void* skyrim_base, void* form, void* keyword_form,
                                        uint64_t singleton_off,
                                        uint64_t allocate_off,
                                        uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !keyword_form) return;

    uint8_t ft = get_form_type(form);
    uint64_t kf_offset = get_keyword_form_offset(ft);
    if (kf_offset == 0) return;

    char* kf_base = reinterpret_cast<char*>(form) + kf_offset;
    void*** kw_array_slot = reinterpret_cast<void***>(kf_base + kKwArrayOff);
    uint32_t* num_kw_ptr  = reinterpret_cast<uint32_t*>(kf_base + kKwCountOff);

    void** old_keywords = *kw_array_slot;
    uint32_t old_count = *num_kw_ptr;
    if (!old_keywords || old_count == 0) return;

    // Find index of the keyword to remove
    uint32_t found_idx = old_count;
    for (uint32_t i = 0; i < old_count; i++) {
        if (old_keywords[i] == keyword_form) {
            found_idx = i;
            break;
        }
    }
    if (found_idx == old_count) return; // not present

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
        return;
    }

    uint32_t new_count = old_count - 1;
    if (new_count == 0) {
        // Last keyword — just null out and free.
        *kw_array_slot = nullptr;
        *num_kw_ptr = 0;
        mm.deallocate(mm.mgr, old_keywords, false);
        return;
    }

    void** fresh = reinterpret_cast<void**>(
        mm.allocate(mm.mgr, new_count * sizeof(void*), 0, false));
    if (!fresh) return;

    // Copy everything except the removed slot
    if (found_idx > 0) {
        std::memcpy(fresh, old_keywords, found_idx * sizeof(void*));
    }
    if (found_idx < new_count) {
        std::memcpy(fresh + found_idx, old_keywords + found_idx + 1,
                    (new_count - found_idx) * sizeof(void*));
    }

    *kw_array_slot = fresh;
    *num_kw_ptr = new_count;
    mm.deallocate(mm.mgr, old_keywords, false);
}

// ── Spell add (NPC only) ────────────────────────────────────────────
// TESSpellList component at npc_layout::kSpellList.
//   +0x00: vtable
//   +0x08: SpellData* actorEffects
// SpellData (0x28 bytes):
//   +0x00: SpellItem** spells
//   +0x18: uint32_t numSpells

extern "C" void mora_rt_add_spell(void* skyrim_base, void* form, void* spell_form,
                                   uint64_t singleton_off,
                                   uint64_t allocate_off,
                                   uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !spell_form) return;

    if (get_form_type(form) != form_type::kNPC) return;

    // TESSpellList is at npc_layout::kSpellList. Its actorEffects field
    // (SpellData*) lives at component + 0x08.
    void** spelldata_slot = reinterpret_cast<void**>(
        reinterpret_cast<char*>(form) + npc_layout::kSpellList + 0x08);
    void* spelldata = *spelldata_slot;
    if (!spelldata) return; // NPC has no spell list — can't extend safely

    void*** spells_slot = reinterpret_cast<void***>(
        reinterpret_cast<char*>(spelldata) + 0x00);
    uint32_t* num_spells_ptr = reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(spelldata) + 0x18);

    void** old_spells = *spells_slot;
    uint32_t old_count = *num_spells_ptr;

    // Idempotent add
    for (uint32_t i = 0; i < old_count; i++) {
        if (old_spells[i] == spell_form) return;
    }

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
        return;
    }

    uint32_t new_count = old_count + 1;
    void** fresh = realloc_ptr_array(mm, old_spells, old_count, new_count);
    if (!fresh) return;

    fresh[old_count] = spell_form;

    *spells_slot = fresh;
    *num_spells_ptr = new_count;

    if (old_spells) {
        mm.deallocate(mm.mgr, old_spells, false);
    }
}

// ── Perk add (NPC only) ─────────────────────────────────────────────
// BGSPerkRankArray component at npc_layout::kPerkArray.
//   +0x00: vtable
//   +0x08: PerkRankData* perks
//   +0x10: uint32_t perkCount
// PerkRankData (0x10 bytes):
//   +0x00: BGSPerk* perk
//   +0x08: int8_t  currentRank
//   +0x09..0x0F: padding

namespace {
constexpr uint64_t kPerkEntrySize = 0x10;
constexpr uint64_t kPerkArrayOff  = 0x08;
constexpr uint64_t kPerkCountOff  = 0x10;
} // anonymous namespace

extern "C" void mora_rt_add_perk(void* skyrim_base, void* form, void* perk_form,
                                  uint64_t singleton_off,
                                  uint64_t allocate_off,
                                  uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !perk_form) return;

    if (get_form_type(form) != form_type::kNPC) return;

    char* pa_base = reinterpret_cast<char*>(form) + npc_layout::kPerkArray;
    char** perks_slot = reinterpret_cast<char**>(pa_base + kPerkArrayOff);
    uint32_t* count_ptr = reinterpret_cast<uint32_t*>(pa_base + kPerkCountOff);

    char* old_perks = *perks_slot;
    uint32_t old_count = *count_ptr;

    // Idempotent add — compare first 8 bytes (the BGSPerk*) of each entry.
    for (uint32_t i = 0; i < old_count; i++) {
        void* entry_perk = *reinterpret_cast<void**>(old_perks + i * kPerkEntrySize);
        if (entry_perk == perk_form) return;
    }

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
        return;
    }

    uint32_t new_count = old_count + 1;
    char* fresh = reinterpret_cast<char*>(
        mm.allocate(mm.mgr, new_count * kPerkEntrySize, 0, false));
    if (!fresh) return;

    if (old_perks && old_count > 0) {
        std::memcpy(fresh, old_perks, old_count * kPerkEntrySize);
    }

    // New entry: BGSPerk* at +0x00, rank 0, rest padding
    char* new_entry = fresh + old_count * kPerkEntrySize;
    std::memset(new_entry, 0, kPerkEntrySize);
    std::memcpy(new_entry, &perk_form, sizeof(void*));

    *perks_slot = fresh;
    *count_ptr = new_count;

    if (old_perks) {
        mm.deallocate(mm.mgr, old_perks, false);
    }
}

// ── Faction add (NPC only) ──────────────────────────────────────────
// NPC factions: BSTArray<FACTION_RANK> at npc_layout::kFactions.
// BSTArrayHeapAllocator calls RE::malloc/RE::free (namespace-resolved),
// which route through MemoryManager — same allocator we use here.
//
// BSTArray<T> layout (BSTArrayHeapAllocator + BSTArrayBase):
//   Allocator part:
//     +0x00: void*    data      (BSTArrayHeapAllocator._data)
//     +0x08: uint32_t capacity  (BSTArrayHeapAllocator._capacity)
//     +0x0C: (pad)
//   BSTArrayBase part:
//     +0x10: uint32_t size      (BSTArrayBase._size)

namespace {
constexpr uint64_t kFactionEntrySize = 0x10;
constexpr uint64_t kBSTArrayDataOff  = 0x00;
constexpr uint64_t kBSTArrayCapOff   = 0x08;
constexpr uint64_t kBSTArraySizeOff  = 0x10;
} // anonymous namespace

extern "C" void mora_rt_add_faction(void* skyrim_base, void* form, void* faction_form,
                                     uint64_t singleton_off,
                                     uint64_t allocate_off,
                                     uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !faction_form) return;

    if (get_form_type(form) != form_type::kNPC) return;

    char* arr_base = reinterpret_cast<char*>(form) + npc_layout::kFactions;
    char** data_slot  = reinterpret_cast<char**>(arr_base + kBSTArrayDataOff);
    uint32_t* cap_ptr  = reinterpret_cast<uint32_t*>(arr_base + kBSTArrayCapOff);
    uint32_t* size_ptr = reinterpret_cast<uint32_t*>(arr_base + kBSTArraySizeOff);

    char* old_data = *data_slot;
    uint32_t old_size = *size_ptr;
    uint32_t old_cap  = *cap_ptr;

    // Idempotent add — match on TESFaction* at +0x00 of each entry.
    for (uint32_t i = 0; i < old_size; i++) {
        void* entry_fac = *reinterpret_cast<void**>(old_data + i * kFactionEntrySize);
        if (entry_fac == faction_form) return;
    }

    uint32_t new_size = old_size + 1;

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
        return;
    }

    // If capacity is sufficient, append in-place (matches BSTArray::emplace_back).
    if (new_size <= old_cap) {
        char* new_entry = old_data + old_size * kFactionEntrySize;
        std::memset(new_entry, 0, kFactionEntrySize);
        std::memcpy(new_entry, &faction_form, sizeof(void*));
        *size_ptr = new_size;
        return;
    }

    // Grow: double capacity (BSTArray default growth factor is 2.0).
    uint32_t new_cap = old_cap > 0
        ? old_cap * 2
        : 4; // BSTArray::DF_CAP

    char* fresh = reinterpret_cast<char*>(
        mm.allocate(mm.mgr, new_cap * kFactionEntrySize, 0, false));
    if (!fresh) return;

    if (old_data && old_size > 0) {
        std::memcpy(fresh, old_data, old_size * kFactionEntrySize);
    }

    // Append new entry
    char* new_entry = fresh + old_size * kFactionEntrySize;
    std::memset(new_entry, 0, kFactionEntrySize);
    std::memcpy(new_entry, &faction_form, sizeof(void*));

    *data_slot = fresh;
    *cap_ptr   = new_cap;
    *size_ptr  = new_size;

    if (old_data) {
        mm.deallocate(mm.mgr, old_data, false);
    }
}
