#pragma once
#include <cstdint>

namespace mora::rt {

// Write operations on raw form memory.
// 'form' is a TESForm* cast to void*.
// These functions use the offset constants from skyrim_abi.h.

// Get form type byte from a TESForm*
inline uint8_t get_form_type(const void* form) {
    return *reinterpret_cast<const uint8_t*>(
        reinterpret_cast<const char*>(form) + 0x1A);
}

// Scalar field writes -- direct memory poke at known offsets
void write_attack_damage(void* form, uint16_t value);
void write_armor_rating(void* form, uint32_t value);
void write_gold_value(void* form, int32_t value);
void write_weight(void* form, float value);

// Returns the offset of the field within the form, or 0 if not applicable.
// Used by the IR emitter to generate direct GEP instructions.
// form_type: FormType byte (0x29=Weapon, 0x1A=Armor, etc.)
// field_id: FieldId value (2=Damage, 3=ArmorRating, 4=GoldValue, 5=Weight)
uint64_t get_field_offset(uint8_t form_type, uint16_t field_id);

// ── Keyword mutation ─────────────────────────────────────────────────
// Requires Skyrim's MemoryManager for heap allocation.
// These functions are called with resolved engine function pointers.
//
// Function pointer types matching Skyrim's MemoryManager:
//   GetSingleton: () → MemoryManager*
//   Allocate: (MemoryManager*, size, alignment, aligned_required) → void*
//   Deallocate: (MemoryManager*, ptr, aligned) → void
//
// Address Library IDs (SE / AE):
//   GetSingleton: 11045 / 11141
//   Allocate:     66859 / 68115
//   Deallocate:   66861 / 68117

using MemMgr_GetSingleton_t = void* (*)();
using MemMgr_Allocate_t = void* (*)(void* mgr, uint64_t size, int32_t align, bool align_req);
using MemMgr_Deallocate_t = void (*)(void* mgr, void* ptr, bool aligned);

// Add a keyword to a form's BGSKeywordForm component.
// skyrim_base: base address of SkyrimSE.exe (for resolving engine functions)
// form: TESForm* pointer
// keyword_form: BGSKeyword* pointer (looked up via BSTHashMap)
// get_singleton_offset: Address Library offset for MemoryManager::GetSingleton
// allocate_offset: Address Library offset for MemoryManager::Allocate
// deallocate_offset: Address Library offset for MemoryManager::Deallocate
} // namespace mora::rt

// Exposed as extern "C" for clean LLVM IR linkage (no name mangling)
extern "C" void mora_rt_add_keyword(void* skyrim_base, void* form, void* keyword_form,
                                     uint64_t get_singleton_offset,
                                     uint64_t allocate_offset,
                                     uint64_t deallocate_offset);
