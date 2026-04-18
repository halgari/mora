#pragma once
#include <cstdint>

namespace mora {

// ---------------------------------------------------------------------------
// Field identifiers
// ---------------------------------------------------------------------------

enum class FieldId : uint16_t {
    // Core fields (original)
    Name         = 1,
    Damage       = 2,
    ArmorRating  = 3,
    GoldValue    = 4,
    Weight       = 5,
    Keywords     = 6,
    Factions     = 7,
    Perks        = 8,
    Spells       = 9,
    Items        = 10,
    Level        = 11,
    Race         = 12,
    EditorId     = 13,
    Shouts       = 14,
    LevSpells    = 15,

    // Weapon scalar fields
    Speed        = 20,
    Reach        = 21,
    Stagger      = 22,
    RangeMin     = 23,
    RangeMax     = 24,
    CritDamage   = 25,
    CritPercent  = 26,

    // Armor scalar fields
    Health       = 30,

    // NPC scalar fields
    CalcLevelMin = 41,
    CalcLevelMax = 42,
    SpeedMult    = 43,

    // Form reference fields
    RaceForm         = 50,
    ClassForm        = 51,
    SkinForm         = 52,
    OutfitForm       = 53,
    EnchantmentForm  = 54,
    VoiceTypeForm    = 55,

    // Leveled list
    LeveledEntries   = 60,
    ChanceNone       = 61,

    // Boolean flags
    ClearAll         = 70,
    AutoCalcStats    = 71,
    Essential        = 72,
    Protected        = 73,

    // Sentinel — returned by action_to_field() when the action name is unknown.
    Invalid          = 0xFF,
};

// ---------------------------------------------------------------------------
// Patch operation
// ---------------------------------------------------------------------------

enum class FieldOp : uint8_t { Set = 0, Add = 1, Remove = 2, Multiply = 3 };

} // namespace mora
