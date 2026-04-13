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
    inline constexpr uint64_t kSpeed        = 0x0C4;  // float
    inline constexpr uint64_t kReach        = 0x0C8;  // float
    inline constexpr uint64_t kStagger      = 0x0D8;  // float
    inline constexpr uint64_t kRangeMin     = 0x0CC;  // float
    inline constexpr uint64_t kRangeMax     = 0x0D0;  // float
    inline constexpr uint64_t kCritDamage   = 0x0E0;  // uint16_t (in CRDT)
    inline constexpr uint64_t kEnchantment  = 0x100;  // TESEnchantment*
} // namespace weapon_layout

namespace armor_layout {
    inline constexpr uint64_t kFullName     = 0x030;
    inline constexpr uint64_t kValueForm    = 0x068;
    inline constexpr uint64_t kWeightForm   = 0x078;
    inline constexpr uint64_t kKeywordForm  = 0x1D8;
    inline constexpr uint64_t kArmorRating  = 0x200;
    inline constexpr uint64_t kHealth       = 0x1F8;  // uint32_t
    inline constexpr uint64_t kEnchantment  = 0x0D8;  // TESEnchantment*
} // namespace armor_layout

namespace npc_layout {
    inline constexpr uint64_t kFullName     = 0x0D8;
    inline constexpr uint64_t kKeywordForm  = 0x110;
    inline constexpr uint64_t kSpellList    = 0x0A0;
    inline constexpr uint64_t kPerkArray    = 0x138;
    inline constexpr uint64_t kFactions     = 0x070; // TESActorBaseData(0x030) + 0x40
    inline constexpr uint64_t kRaceForm     = 0x150;
    inline constexpr uint64_t kClassForm    = 0x158; // TESClass*
    inline constexpr uint64_t kSkinForm     = 0x170; // TESObjectARMO* (skin)
    inline constexpr uint64_t kOutfitForm   = 0x1C0; // BGSOutfit* (default outfit)
    inline constexpr uint64_t kVoiceType    = 0x168; // BGSVoiceType*
    inline constexpr uint64_t kLevel        = 0x048; // uint16_t (TESActorBaseData)
    inline constexpr uint64_t kCalcLevelMin = 0x04A; // uint16_t
    inline constexpr uint64_t kCalcLevelMax = 0x04C; // uint16_t
    inline constexpr uint64_t kSpeedMult    = 0x04E; // uint16_t (x100)
    inline constexpr uint64_t kFlags        = 0x040; // uint32_t (TESActorBaseData flags)
} // namespace npc_layout

// TESLevItem / TESLevCharacter: TESLeveledList component at offset 0x30
// (both inherit TESBoundObject(0x30) + TESLeveledList(0x28))
namespace leveled_list_layout {
    inline constexpr uint64_t kComponent   = 0x030; // offset of TESLeveledList within TESLevItem
    // TESLeveledList internal layout (relative to component start):
    //   +0x00: vtable (BaseFormComponent)
    //   +0x08: SimpleArray<LEVELED_OBJECT> entries (just a pointer)
    //   +0x10: int8_t chanceNone
    //   +0x11: uint8_t llFlags
    //   +0x12: uint8_t numEntries
    //   +0x20: TESGlobal* chanceGlobal
    inline constexpr uint64_t kEntries     = 0x08;  // SimpleArray<LEVELED_OBJECT>* (pointer)
    inline constexpr uint64_t kChanceNone  = 0x10;  // int8_t (0-100)
    inline constexpr uint64_t kFlags       = 0x11;  // uint8_t
    inline constexpr uint64_t kNumEntries  = 0x12;  // uint8_t
    inline constexpr uint64_t kChanceGlobal = 0x20; // TESGlobal*
} // namespace leveled_list_layout

// LEVELED_OBJECT element layout (0x18 bytes each)
namespace leveled_object {
    inline constexpr uint64_t kSize      = 0x18;
    inline constexpr uint64_t kForm      = 0x00; // TESForm*
    inline constexpr uint64_t kCount     = 0x08; // uint16_t
    inline constexpr uint64_t kLevel     = 0x0A; // uint16_t
    inline constexpr uint64_t kItemExtra = 0x10; // ContainerItemExtra*
    inline constexpr uint8_t  kMaxEntries = 255; // uint8_t numEntries max
} // namespace leveled_object

namespace form_type {
    inline constexpr uint8_t kLeveledItem = 0x2D;
    inline constexpr uint8_t kLeveledChar = 0x2C;
} // namespace form_type

// Component member offset (vtable at +0x00, first data member at +0x08)
inline constexpr uint64_t kComponentMember = 0x08;

} // namespace mora
