#pragma once
#include <cstdint>
#include "mora/data/form_model.h"

// Mora runtime form operations.
//
// These helpers are compiled into mora_rt.lib and linked into every generated
// patch DLL. The IR emitter produces calls to the extern "C" functions below.
//
// Post-CommonLibSSE-NG migration: all address resolution is handled internally
// by CommonLib (RE::TESForm::LookupByID, RE::malloc/free, etc.). No more
// skyrim_base or Address Library offset parameters.
//
// All functions are safe to call with nullptr forms/targets — they no-op.

namespace mora::rt {

// Read the form type byte from a TESForm*.
// On Windows, we could use RE::TESForm::GetFormType(), but this raw read
// is also needed by the IR codegen path and is verified in form_model_verify.cpp.
inline uint8_t get_form_type(const void* form) {
    return *reinterpret_cast<const uint8_t*>(
        reinterpret_cast<const char*>(form) + model::kFormTypeOffset);
}

// Return the offset of a scalar field within the form, or 0 if not applicable.
// Used by the IR emitter to emit direct GEP+store instructions for scalar
// fields (Damage, ArmorRating, GoldValue, Weight).
uint64_t get_field_offset(uint8_t form_type, uint16_t field_id);

} // namespace mora::rt

// ── RT entry points (extern "C" for clean LLVM IR linkage) ──────────
//
// Semantics: all "add" operations are idempotent — if the entry is already
// present the call returns without modification. All operations no-op on
// nullptr or form-type mismatch.

extern "C" {

// Look up a form by FormID via CommonLibSSE-NG.
void* mora_rt_lookup_form(uint32_t formid);

// Write a name (BSFixedString) to a form's TESFullName component.
// Works for NPC / Weapon / Armor (form type determined at runtime).
void mora_rt_write_name(void* form, const char* name);

// Add a keyword to a form's BGSKeywordForm component (NPC / Weapon / Armor).
void mora_rt_add_keyword(void* form, void* keyword_form);

// Remove a keyword from a form's BGSKeywordForm component (NPC / Weapon / Armor).
void mora_rt_remove_keyword(void* form, void* keyword_form);

// Add a spell to an NPC's TESSpellList.
void mora_rt_add_spell(void* form, void* spell_form);

// Remove a spell from an NPC's TESSpellList.
void mora_rt_remove_spell(void* form, void* spell_form);

// Add a perk to an NPC's BGSPerkRankArray at rank 0.
void mora_rt_add_perk(void* form, void* perk_form);

// Add a faction to an NPC's faction BSTArray at rank 0.
void mora_rt_add_faction(void* form, void* faction_form);

// Add a shout to an NPC's TESSpellList shout array.
void mora_rt_add_shout(void* form, void* shout_form);

// Add an entry to a TESLeveledList.
void mora_rt_add_to_leveled_list(void* form, uint32_t entry_formid,
                                  uint16_t level, uint16_t count);

// Remove entries matching a FormID from a TESLeveledList.
void mora_rt_remove_from_leveled_list(void* form, uint32_t entry_formid);

// Set the chanceNone field on a TESLeveledList.
void mora_rt_set_chance_none(void* form, int8_t chance);

// Clear all entries from a TESLeveledList.
void mora_rt_clear_leveled_list(void* form);

} // extern "C"
