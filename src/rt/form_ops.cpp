#include "mora/rt/form_ops.h"
#include <cstring>

// Offset constants matching skyrim_abi.h.
// Reproduced here to avoid pulling in the _WIN32-guarded header on Linux.
namespace {

// FormType bytes
constexpr uint8_t kWeapon = 0x29;
constexpr uint8_t kArmor  = 0x1A;

// FieldId values (from patch_set.h FieldId enum)
constexpr uint16_t kDamage      = 2;
constexpr uint16_t kArmorRating = 3;
constexpr uint16_t kGoldValue   = 4;
constexpr uint16_t kWeight      = 5;

// Weapon component offsets (from skyrim_abi.h weapon_offsets)
constexpr uint64_t kWeaponAttackDamage = 0x0C0; // TESAttackDamageForm base
constexpr uint64_t kWeaponValueForm    = 0x0A0; // TESValueForm base
constexpr uint64_t kWeaponWeightForm   = 0x0B0; // TESWeightForm base

// Armor component offsets (from skyrim_abi.h armor_offsets)
constexpr uint64_t kArmorRatingOffset  = 0x200; // direct uint32_t
constexpr uint64_t kArmorValueForm     = 0x068; // TESValueForm base
constexpr uint64_t kArmorWeightForm    = 0x078; // TESWeightForm base

// Component member offsets (vtable at +0x00, member at +0x08)
constexpr uint64_t kComponentMember    = 0x08;

} // anonymous namespace

namespace mora::rt {

void write_attack_damage(void* form, uint16_t value) {
    auto* dst = reinterpret_cast<char*>(form) + kWeaponAttackDamage + kComponentMember;
    std::memcpy(dst, &value, sizeof(value));
}

void write_armor_rating(void* form, uint32_t value) {
    auto* dst = reinterpret_cast<char*>(form) + kArmorRatingOffset;
    std::memcpy(dst, &value, sizeof(value));
}

void write_gold_value(void* form, int32_t value) {
    // Caller must know the form type to pick the right base offset.
    // This function assumes Weapon layout. For Armor, use write_gold_value_armor.
    // In practice, the IR emitter uses get_field_offset() and emits stores directly.
    auto* dst = reinterpret_cast<char*>(form) + kWeaponValueForm + kComponentMember;
    std::memcpy(dst, &value, sizeof(value));
}

void write_weight(void* form, float value) {
    // Same caveat as write_gold_value -- assumes Weapon layout.
    auto* dst = reinterpret_cast<char*>(form) + kWeaponWeightForm + kComponentMember;
    std::memcpy(dst, &value, sizeof(value));
}

uint64_t get_field_offset(uint8_t form_type, uint16_t field_id) {
    if (form_type == kWeapon) {
        switch (field_id) {
            case kDamage:    return kWeaponAttackDamage + kComponentMember;  // uint16_t
            case kGoldValue: return kWeaponValueForm    + kComponentMember;  // int32_t
            case kWeight:    return kWeaponWeightForm   + kComponentMember;  // float
            default:         return 0;
        }
    }
    if (form_type == kArmor) {
        switch (field_id) {
            case kArmorRating: return kArmorRatingOffset;                     // uint32_t
            case kGoldValue:   return kArmorValueForm   + kComponentMember;   // int32_t
            case kWeight:      return kArmorWeightForm  + kComponentMember;   // float
            default:           return 0;
        }
    }
    return 0;
}

} // namespace mora::rt
