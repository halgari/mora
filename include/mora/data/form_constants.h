#pragma once
// Platform-neutral Skyrim form type and layout constants.
// Extracted from skyrim_abi.h so that cross-platform code (codegen, rt)
// can reference them without pulling in the _WIN32-guarded ABI header.

#include <cstdint>

namespace mora {

// FormType bytes (subset matching skyrim_abi.h FormType enum)
namespace form_type {
    inline constexpr uint8_t kKeyword = 0x04;
    inline constexpr uint8_t kFaction = 0x06;
    inline constexpr uint8_t kRace    = 0x0A;
    inline constexpr uint8_t kSpell   = 0x16;
    inline constexpr uint8_t kArmor   = 0x1A;
    inline constexpr uint8_t kWeapon  = 0x29;
    inline constexpr uint8_t kNPC     = 0x2B;
    inline constexpr uint8_t kPerk    = 0x31;
} // namespace form_type

// Component offsets within derived form structs.
// These are the byte offsets from the start of the derived class
// to the start of each component base class.
namespace weapon_layout {
    inline constexpr uint64_t kFullName     = 0x030;
    inline constexpr uint64_t kValueForm    = 0x0A0;
    inline constexpr uint64_t kWeightForm   = 0x0B0;
    inline constexpr uint64_t kAttackDamage = 0x0C0;
    inline constexpr uint64_t kKeywordForm  = 0x140;
} // namespace weapon_layout

namespace armor_layout {
    inline constexpr uint64_t kFullName     = 0x030;
    inline constexpr uint64_t kValueForm    = 0x068;
    inline constexpr uint64_t kWeightForm   = 0x078;
    inline constexpr uint64_t kKeywordForm  = 0x1D8;
    inline constexpr uint64_t kArmorRating  = 0x200;
} // namespace armor_layout

namespace npc_layout {
    inline constexpr uint64_t kFullName     = 0x0D8;
    inline constexpr uint64_t kKeywordForm  = 0x110;
    inline constexpr uint64_t kSpellList    = 0x0A0;
    inline constexpr uint64_t kPerkArray    = 0x138;
    inline constexpr uint64_t kFactions     = 0x070; // TESActorBaseData(0x030) + 0x40
    inline constexpr uint64_t kRaceForm     = 0x150;
} // namespace npc_layout

// Component member offset (vtable at +0x00, first data member at +0x08)
inline constexpr uint64_t kComponentMember = 0x08;

} // namespace mora
