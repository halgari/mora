#include "mora/rt/form_ops.h"
#include "mora/rt/bst_hashmap.h"
#include "mora/data/form_constants.h"
#include "mora/data/action_names.h"
#include <cstring>

using namespace mora::form_type;
using namespace mora;

namespace {

// Use fid16() to convert FieldId enum to uint16_t for switch cases
using mora::fid16;
using mora::FieldId;

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
            case fid16(FieldId::Damage):          return weapon_layout::kAttackDamage + kComponentMember;
            case fid16(FieldId::GoldValue):       return weapon_layout::kValueForm    + kComponentMember;
            case fid16(FieldId::Weight):          return weapon_layout::kWeightForm   + kComponentMember;
            case fid16(FieldId::Speed):           return weapon_layout::kSpeed;
            case fid16(FieldId::Reach):           return weapon_layout::kReach;
            case fid16(FieldId::Stagger):         return weapon_layout::kStagger;
            case fid16(FieldId::RangeMin):        return weapon_layout::kRangeMin;
            case fid16(FieldId::RangeMax):        return weapon_layout::kRangeMax;
            case fid16(FieldId::CritDamage):      return weapon_layout::kCritDamage;
            case fid16(FieldId::EnchantmentForm): return weapon_layout::kEnchantment;
            default: return 0;
        }
    }
    if (ft == form_type::kArmor) {
        switch (field_id) {
            case fid16(FieldId::ArmorRating):     return armor_layout::kArmorRating;
            case fid16(FieldId::GoldValue):       return armor_layout::kValueForm   + kComponentMember;
            case fid16(FieldId::Weight):          return armor_layout::kWeightForm  + kComponentMember;
            case fid16(FieldId::Health):          return armor_layout::kHealth;
            case fid16(FieldId::EnchantmentForm): return armor_layout::kEnchantment;
            default: return 0;
        }
    }
    if (ft == form_type::kNPC) {
        switch (field_id) {
            case fid16(FieldId::Level):           return npc_layout::kLevel;
            case fid16(FieldId::CalcLevelMin):    return npc_layout::kCalcLevelMin;
            case fid16(FieldId::CalcLevelMax):    return npc_layout::kCalcLevelMax;
            case fid16(FieldId::RaceForm):        return npc_layout::kRaceForm;
            case fid16(FieldId::ClassForm):       return npc_layout::kClassForm;
            case fid16(FieldId::SkinForm):        return npc_layout::kSkinForm;
            case fid16(FieldId::OutfitForm):      return npc_layout::kOutfitForm;
            case fid16(FieldId::VoiceTypeForm):   return npc_layout::kVoiceType;
            default: return 0;
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

// ── Spell remove (NPC only) ────────────────────────────────────────
// Removes a spell from the TESSpellList by compacting the array.

extern "C" void mora_rt_remove_spell(void* skyrim_base, void* form, void* spell_form,
                                      uint64_t singleton_off,
                                      uint64_t allocate_off,
                                      uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !spell_form) return;
    if (get_form_type(form) != form_type::kNPC) return;

    void** spelldata_slot = reinterpret_cast<void**>(
        reinterpret_cast<char*>(form) + npc_layout::kSpellList + 0x08);
    void* spelldata = *spelldata_slot;
    if (!spelldata) return;

    void*** spells_slot = reinterpret_cast<void***>(
        reinterpret_cast<char*>(spelldata) + 0x00);
    uint32_t* num_spells_ptr = reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(spelldata) + 0x18);

    void** old_spells = *spells_slot;
    uint32_t old_count = *num_spells_ptr;

    // Find the spell to remove
    uint32_t found = UINT32_MAX;
    for (uint32_t i = 0; i < old_count; i++) {
        if (old_spells[i] == spell_form) { found = i; break; }
    }
    if (found == UINT32_MAX) return; // not present

    // Compact: shift remaining entries down
    for (uint32_t i = found; i + 1 < old_count; i++) {
        old_spells[i] = old_spells[i + 1];
    }
    *num_spells_ptr = old_count - 1;
}

// ── Shout add (NPC only) ───────────────────────────────────────────
// TESSpellList also holds shouts. Shouts are stored similarly to spells
// in a separate array within the SpellData struct:
//   SpellData + 0x08: TESShout** shouts
//   SpellData + 0x1C: uint32_t   numShouts

extern "C" void mora_rt_add_shout(void* skyrim_base, void* form, void* shout_form,
                                   uint64_t singleton_off,
                                   uint64_t allocate_off,
                                   uint64_t deallocate_off) {
    using namespace mora::rt;
    if (!form || !shout_form) return;
    if (get_form_type(form) != form_type::kNPC) return;

    void** spelldata_slot = reinterpret_cast<void**>(
        reinterpret_cast<char*>(form) + npc_layout::kSpellList + 0x08);
    void* spelldata = *spelldata_slot;
    if (!spelldata) return;

    void*** shouts_slot = reinterpret_cast<void***>(
        reinterpret_cast<char*>(spelldata) + 0x08);
    uint32_t* num_shouts_ptr = reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(spelldata) + 0x1C);

    void** old_shouts = *shouts_slot;
    uint32_t old_count = *num_shouts_ptr;

    // Idempotent add
    for (uint32_t i = 0; i < old_count; i++) {
        if (old_shouts[i] == shout_form) return;
    }

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
        return;
    }

    uint32_t new_count = old_count + 1;
    void** fresh = realloc_ptr_array(mm, old_shouts, old_count, new_count);
    if (!fresh) return;

    fresh[old_count] = shout_form;
    *shouts_slot = fresh;
    *num_shouts_ptr = new_count;

    if (old_shouts) {
        mm.deallocate(mm.mgr, old_shouts, false);
    }
}

// ── Leveled List operations ────────────────────────────────────────
// TESLeveledList component sits at leveled_list_layout::kComponent (0x30)
// within TESLevItem. It uses a SimpleArray<LEVELED_OBJECT> for entries.
//
// SimpleArray stores entries as a raw pointer. The count is at
// leveled_list_layout::kNumEntries (uint8_t at component+0x12).
// Each LEVELED_OBJECT is 0x18 bytes: {form*, count(u16), level(u16), pad, itemExtra*}

namespace {
    using namespace mora::leveled_list_layout;
    using namespace mora::leveled_object;

    // Get pointer to the TESLeveledList component within a leveled form
    char* get_ll_component(void* form) {
        return reinterpret_cast<char*>(form) + kComponent;
    }

    // Read the entries pointer from the SimpleArray
    char* get_ll_entries(char* comp) {
        void** entries_slot = reinterpret_cast<void**>(comp + kEntries);
        return reinterpret_cast<char*>(*entries_slot);
    }

    uint8_t get_ll_count(char* comp) {
        return *reinterpret_cast<uint8_t*>(comp + kNumEntries);
    }

    void set_ll_count(char* comp, uint8_t count) {
        *reinterpret_cast<uint8_t*>(comp + kNumEntries) = count;
    }

    // Read the FormID (first 4 bytes of the form pointer's FormID field at +0x14)
    uint32_t read_entry_formid(char* entry) {
        void* entry_form = *reinterpret_cast<void**>(entry + leveled_object::kForm);
        if (!entry_form) return 0;
        return *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(entry_form) + 0x14);
    }
} // anonymous namespace

extern "C" void mora_rt_add_to_leveled_list(void* skyrim_base, void* form,
                                              uint32_t entry_formid, uint16_t level, uint16_t count,
                                              uint64_t singleton_off,
                                              uint64_t allocate_off,
                                              uint64_t deallocate_off,
                                              const void* form_map) {
    using namespace mora::rt;
    if (!form) return;

    uint8_t ft = get_form_type(form);
    if (ft != form_type::kLeveledItem && ft != form_type::kLeveledChar) return;

    // Look up the form to add
    auto* map = reinterpret_cast<const mora::rt::BSTHashMapLayout*>(form_map);
    void* entry_form = bst_hashmap_lookup(map, entry_formid);
    if (!entry_form) return;

    char* comp = get_ll_component(form);
    char* old_entries = get_ll_entries(comp);
    uint8_t old_count = get_ll_count(comp);

    // Check for duplicates (same form + same level)
    for (uint8_t i = 0; i < old_count; i++) {
        char* e = old_entries + i * kSize;
        void* e_form = *reinterpret_cast<void**>(e + leveled_object::kForm);
        uint16_t e_level = *reinterpret_cast<uint16_t*>(e + leveled_object::kLevel);
        if (e_form == entry_form && e_level == level) return; // already present
    }

    if (old_count >= kMaxEntries) return; // can't add more

    MemMgrFns mm;
    if (!resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) return;

    uint8_t new_count = old_count + 1;
    char* fresh = reinterpret_cast<char*>(
        mm.allocate(mm.mgr, new_count * kSize, 0, false));
    if (!fresh) return;

    // Copy old entries
    if (old_entries && old_count > 0) {
        std::memcpy(fresh, old_entries, old_count * kSize);
    }

    // Write new entry at the end
    char* new_entry = fresh + old_count * kSize;
    std::memset(new_entry, 0, kSize);
    std::memcpy(new_entry + leveled_object::kForm, &entry_form, sizeof(void*));
    std::memcpy(new_entry + leveled_object::kCount, &count, sizeof(uint16_t));
    std::memcpy(new_entry + leveled_object::kLevel, &level, sizeof(uint16_t));

    // Sort by level (insertion sort — entries are usually nearly sorted)
    for (uint8_t i = 1; i < new_count; i++) {
        uint16_t cur_level = *reinterpret_cast<uint16_t*>(fresh + i * kSize + leveled_object::kLevel);
        if (i > 0) {
            uint16_t prev_level = *reinterpret_cast<uint16_t*>(fresh + (i-1) * kSize + leveled_object::kLevel);
            if (cur_level < prev_level) {
                // Swap backwards until sorted
                char tmp[0x18];
                std::memcpy(tmp, fresh + i * kSize, kSize);
                uint8_t j = i;
                while (j > 0) {
                    uint16_t jl = *reinterpret_cast<uint16_t*>(fresh + (j-1) * kSize + leveled_object::kLevel);
                    if (jl <= cur_level) break;
                    std::memcpy(fresh + j * kSize, fresh + (j-1) * kSize, kSize);
                    j--;
                }
                std::memcpy(fresh + j * kSize, tmp, kSize);
            }
        }
    }

    // Update the leveled list
    void** entries_slot = reinterpret_cast<void**>(comp + kEntries);
    *entries_slot = fresh;
    set_ll_count(comp, new_count);

    if (old_entries) {
        mm.deallocate(mm.mgr, old_entries, false);
    }
}

extern "C" void mora_rt_remove_from_leveled_list(void* form, uint32_t entry_formid) {
    using namespace mora::rt;
    if (!form) return;

    uint8_t ft = get_form_type(form);
    if (ft != form_type::kLeveledItem && ft != form_type::kLeveledChar) return;

    char* comp = get_ll_component(form);
    char* entries = get_ll_entries(comp);
    uint8_t count = get_ll_count(comp);
    if (!entries || count == 0) return;

    // Compact: remove all entries matching the FormID
    uint8_t write = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint32_t fid = read_entry_formid(entries + i * kSize);
        if (fid != entry_formid) {
            if (write != i) {
                std::memcpy(entries + write * kSize, entries + i * kSize, kSize);
            }
            write++;
        }
    }
    set_ll_count(comp, write);
}

extern "C" void mora_rt_set_chance_none(void* form, int8_t chance) {
    using namespace mora::rt;
    if (!form) return;

    uint8_t ft = get_form_type(form);
    if (ft != form_type::kLeveledItem && ft != form_type::kLeveledChar) return;

    char* comp = get_ll_component(form);
    *reinterpret_cast<int8_t*>(comp + kChanceNone) = chance;
}

extern "C" void mora_rt_clear_leveled_list(void* form,
                                            uint64_t singleton_off,
                                            uint64_t allocate_off,
                                            uint64_t deallocate_off,
                                            void* skyrim_base) {
    using namespace mora::rt;
    if (!form) return;

    uint8_t ft = get_form_type(form);
    if (ft != form_type::kLeveledItem && ft != form_type::kLeveledChar) return;

    char* comp = get_ll_component(form);
    char* old_entries = get_ll_entries(comp);

    if (old_entries) {
        MemMgrFns mm;
        if (resolve_memmgr(skyrim_base, singleton_off, allocate_off, deallocate_off, mm)) {
            mm.deallocate(mm.mgr, old_entries, false);
        }
    }

    void** entries_slot = reinterpret_cast<void**>(comp + kEntries);
    *entries_slot = nullptr;
    set_ll_count(comp, 0);
}
