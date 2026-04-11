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

// ── Keyword mutation via Skyrim's MemoryManager ────────────────────

// BGSKeywordForm layout (same across NPC_, WEAP, ARMO — offset varies by form type)
// +0x00: vtable
// +0x08: BGSKeyword** keywords
// +0x10: uint32_t numKeywords
// +0x14: uint32_t pad

// NPC keyword form offset: 0x110
// Weapon keyword form offset: 0x140
// Armor keyword form offset: 0x1D8

constexpr uint8_t kNPC = 0x2B;

static uint64_t get_keyword_form_offset(uint8_t form_type) {
    switch (form_type) {
        case kNPC:    return 0x110;
        case kWeapon: return 0x140;
        case kArmor:  return 0x1D8;
        default:      return 0;
    }
}

} // close mora::rt namespace

extern "C" void mora_rt_add_keyword(void* skyrim_base, void* form, void* keyword_form,
                                     uint64_t get_singleton_offset,
                                     uint64_t allocate_offset,
                                     uint64_t deallocate_offset) {
    using namespace mora::rt;
    if (!form || !keyword_form) return;

    uint8_t form_type = get_form_type(form);
    uint64_t kf_offset = get_keyword_form_offset(form_type);
    if (kf_offset == 0) return;

    // Get pointers to the keyword form's members
    char* kf_base = reinterpret_cast<char*>(form) + kf_offset;
    void**   kw_array_ptr = reinterpret_cast<void**>(kf_base + 0x08);
    uint32_t* num_kw_ptr  = reinterpret_cast<uint32_t*>(kf_base + 0x10);

    void** old_keywords = *reinterpret_cast<void***>(kw_array_ptr);
    uint32_t old_count = *num_kw_ptr;

    // Check if keyword already present
    for (uint32_t i = 0; i < old_count; i++) {
        if (old_keywords[i] == keyword_form) return; // already has it
    }

    // Resolve MemoryManager functions from skyrim_base + offsets
    auto get_singleton = reinterpret_cast<MemMgr_GetSingleton_t>(
        reinterpret_cast<char*>(skyrim_base) + get_singleton_offset);
    auto allocate = reinterpret_cast<MemMgr_Allocate_t>(
        reinterpret_cast<char*>(skyrim_base) + allocate_offset);
    auto deallocate = reinterpret_cast<MemMgr_Deallocate_t>(
        reinterpret_cast<char*>(skyrim_base) + deallocate_offset);

    void* mgr = get_singleton();
    if (!mgr) return;

    // Allocate new array
    uint32_t new_count = old_count + 1;
    void** new_keywords = reinterpret_cast<void**>(
        allocate(mgr, new_count * sizeof(void*), 0, false));
    if (!new_keywords) return;

    // Copy old + append new
    if (old_keywords && old_count > 0) {
        std::memcpy(new_keywords, old_keywords, old_count * sizeof(void*));
    }
    new_keywords[old_count] = keyword_form;

    // Update the form
    *reinterpret_cast<void***>(kw_array_ptr) = new_keywords;
    *num_kw_ptr = new_count;

    // Free old array
    if (old_keywords) {
        deallocate(mgr, old_keywords, false);
    }
}
