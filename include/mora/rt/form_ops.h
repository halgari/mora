#pragma once
#include <cstdint>

// Mora runtime form operations.
//
// These helpers are compiled into mora_rt.lib and linked into every generated
// patch DLL via LTO. The IR emitter produces calls to the extern "C" functions
// below, passing Address Library offsets as constants so LTO can inline and
// constant-fold the whole thing.
//
// All functions are safe to call with nullptr forms/targets — they no-op.

namespace mora::rt {

// Read the form type byte from a TESForm*.
inline uint8_t get_form_type(const void* form) {
    return *reinterpret_cast<const uint8_t*>(
        reinterpret_cast<const char*>(form) + 0x1A);
}

// Return the offset of a scalar field within the form, or 0 if not applicable.
// Used by the IR emitter to emit direct GEP+store instructions for scalar
// fields (Damage, ArmorRating, GoldValue, Weight).
uint64_t get_field_offset(uint8_t form_type, uint16_t field_id);

// ── MemoryManager function types ────────────────────────────────────
// Resolved at runtime from (skyrim_base + AddressLibrary offset).
//
// Address Library IDs (SE / AE):
//   GetSingleton: 11045 / 11141
//   Allocate:     66859 / 68115
//   Deallocate:   66861 / 68117

using MemMgr_GetSingleton_t = void* (*)();
using MemMgr_Allocate_t = void* (*)(void* mgr, uint64_t size, int32_t align, bool align_req);
using MemMgr_Deallocate_t = void (*)(void* mgr, void* ptr, bool aligned);

// ── BSFixedString function types ────────────────────────────────────
// ctor8 interns a const char* → BSFixedString.
// release8 decrements the refcount (frees if zero).
//
// Address Library IDs (SE / AE):
//   ctor8:    67819 / 69161
//   release8: 67847 / 69192

using BSFixedString_ctor8_t = void* (*)(void* self, const char* data);
using BSFixedString_release8_t = void (*)(const char** entry);

} // namespace mora::rt

// ── RT entry points (extern "C" for clean LLVM IR linkage) ──────────
//
// All functions take `skyrim_base` (the base address of SkyrimSE.exe) plus
// Address Library offsets for the engine functions they call. The IR emitter
// resolves those offsets at compile time and passes them as LLVM constants,
// so after LTO the calls are fully inlined and the MemoryManager/BSFixedString
// function pointers are loaded from known displacements.
//
// Semantics: all "add" operations are idempotent — if the entry is already
// present the call returns without modification. All operations no-op on
// nullptr or form-type mismatch.

extern "C" {

// Write a name (BSFixedString) to a form's TESFullName component.
// Works for NPC / Weapon / Armor (form type determined at runtime).
void mora_rt_write_name(void* skyrim_base, void* form, const char* name,
                         uint64_t ctor8_offset, uint64_t release8_offset);

// Add a keyword to a form's BGSKeywordForm component (NPC / Weapon / Armor).
void mora_rt_add_keyword(void* skyrim_base, void* form, void* keyword_form,
                          uint64_t singleton_off,
                          uint64_t allocate_off,
                          uint64_t deallocate_off);

// Remove a keyword from a form's BGSKeywordForm component (NPC / Weapon / Armor).
// No-op if the keyword is not present.
void mora_rt_remove_keyword(void* skyrim_base, void* form, void* keyword_form,
                             uint64_t singleton_off,
                             uint64_t allocate_off,
                             uint64_t deallocate_off);

// Add a spell to an NPC's TESSpellList. No-op on non-NPC forms or NPCs
// without an existing SpellData block.
void mora_rt_add_spell(void* skyrim_base, void* form, void* spell_form,
                        uint64_t singleton_off,
                        uint64_t allocate_off,
                        uint64_t deallocate_off);

// Add a perk to an NPC's BGSPerkRankArray at rank 0. No-op on non-NPC forms.
void mora_rt_add_perk(void* skyrim_base, void* form, void* perk_form,
                       uint64_t singleton_off,
                       uint64_t allocate_off,
                       uint64_t deallocate_off);

// Add a faction to an NPC's faction BSTArray at rank 0.
// BSTArray's allocator (BSTArrayHeapAllocator) calls RE::malloc/RE::free
// which route through MemoryManager — same allocator used here.
// Respects BSTArray capacity (appends in-place when possible, grows 2x).
void mora_rt_add_faction(void* skyrim_base, void* form, void* faction_form,
                          uint64_t singleton_off,
                          uint64_t allocate_off,
                          uint64_t deallocate_off);

// Remove a spell from an NPC's TESSpellList. Compacts the array in-place.
void mora_rt_remove_spell(void* skyrim_base, void* form, void* spell_form,
                           uint64_t singleton_off,
                           uint64_t allocate_off,
                           uint64_t deallocate_off);

// Add a shout to an NPC's TESSpellList shout array.
void mora_rt_add_shout(void* skyrim_base, void* form, void* shout_form,
                        uint64_t singleton_off,
                        uint64_t allocate_off,
                        uint64_t deallocate_off);

// Add an entry to a TESLeveledList's SimpleArray.
// Parameters packed as: value = formid, extra args via level/count encoding.
// level and count are passed via the patch entry's value field as:
//   value = (uint64_t)formid | ((uint64_t)level << 32) | ((uint64_t)count << 48)
void mora_rt_add_to_leveled_list(void* skyrim_base, void* form,
                                  uint32_t entry_formid, uint16_t level, uint16_t count,
                                  uint64_t singleton_off,
                                  uint64_t allocate_off,
                                  uint64_t deallocate_off,
                                  const void* form_map);

// Remove entries matching a FormID from a TESLeveledList.
void mora_rt_remove_from_leveled_list(void* form, uint32_t entry_formid);

// Set the chanceNone field on a TESLeveledList.
void mora_rt_set_chance_none(void* form, int8_t chance);

// Clear all entries from a TESLeveledList.
void mora_rt_clear_leveled_list(void* form,
                                 uint64_t singleton_off,
                                 uint64_t allocate_off,
                                 uint64_t deallocate_off,
                                 void* skyrim_base);

} // extern "C"
