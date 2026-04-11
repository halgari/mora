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

} // namespace mora::rt
