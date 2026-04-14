#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// Unified component-based data model for Skyrim forms.
//
// Single source of truth for:
//   - In-memory layout (byte offsets for GEP+store and patch_walker)
//   - ESP reading (record type, subrecord tag, offset within subrecord)
//   - Action names (set_gold_value, add_keyword, etc.)
//   - Field metadata (FieldId, value type, polymorphic applicability)
//
// Pure constexpr — no platform-specific headers. The Windows build verifies
// all offsets against CommonLibSSE-NG in form_model_verify.cpp.
// ═══════════════════════════════════════════════════════════════════════════

#include "mora/eval/patch_set.h"   // FieldId, FieldOp
#include "mora/ast/types.h"        // TypeKind
#include <cstdint>
#include <cstring>

namespace mora::model {

// ── Value types ────────────────────────────────────────────────────────
// Unifies ReadType (ESP parsing) and StoreKind (IR emitter / patch_walker).

enum class ValueType : uint8_t {
    Int8, Int16, Int32, UInt8, UInt16, UInt32,
    Float32, FormRef, BSFixedString,
};

// ── Component definitions ──────────────────────────────────────────────
// A component is a Skyrim base class (TESValueForm, BGSKeywordForm, etc.)
// that provides one or more data members.

struct ComponentMember {
    const char* name;        // "value", "weight", "attackDamage"
    uint16_t    offset;      // byte offset within the component
    ValueType   value_type;
};

struct ComponentDef {
    const char*            name;
    const ComponentMember* members;
    uint8_t                member_count;
    enum class Kind : uint8_t {
        Scalar,     // direct GEP+store (TESValueForm, TESWeightForm, etc.)
        String,     // BSFixedString write via mora_rt_write_name
        FormArray,  // add/remove via RT function (BGSKeywordForm, TESSpellList)
        Flags,      // bit manipulation on a uint32_t flags word
    } kind;
};

// ── Component member tables ────────────────────────────────────────────

inline constexpr ComponentMember kValueFormMembers[] = {
    {"value", 0x08, ValueType::Int32},
};

inline constexpr ComponentMember kWeightFormMembers[] = {
    {"weight", 0x08, ValueType::Float32},
};

inline constexpr ComponentMember kAttackDamageFormMembers[] = {
    {"attackDamage", 0x08, ValueType::UInt16},
};

inline constexpr ComponentMember kFullNameMembers[] = {
    {"fullName", 0x08, ValueType::BSFixedString},
};

inline constexpr ComponentMember kKeywordFormMembers[] = {
    {"keywords", 0x08, ValueType::FormRef},   // BGSKeyword** pointer
    {"numKeywords", 0x10, ValueType::UInt32},
};

inline constexpr ComponentMember kEnchantableFormMembers[] = {
    {"formEnchanting", 0x08, ValueType::FormRef},  // EnchantmentItem*
};

// Weapon-specific direct members (offsets are absolute from form start)
// Verified against CommonLibSSE-NG: TESObjectWEAP::weaponData at 0x168,
// TESObjectWEAP::criticalData at 0x1A0
inline constexpr ComponentMember kWeaponDirectMembers[] = {
    {"speed",      0x170, ValueType::Float32},  // 0 — weaponData(0x168) + Data::speed(0x08)
    {"reach",      0x174, ValueType::Float32},  // 1 — weaponData(0x168) + Data::reach(0x0C)
    {"rangeMin",   0x178, ValueType::Float32},  // 2 — weaponData(0x168) + Data::minRange(0x10)
    {"rangeMax",   0x17C, ValueType::Float32},  // 3 — weaponData(0x168) + Data::maxRange(0x14)
    {"stagger",    0x188, ValueType::Float32},  // 4 — weaponData(0x168) + Data::staggerValue(0x20)
    {"critDamage", 0x1B0, ValueType::UInt16},   // 5 — criticalData(0x1A0) + CriticalData::damage(0x10)
};

// Armor-specific direct members
// Verified against CommonLibSSE-NG: TESObjectARMO
inline constexpr ComponentMember kArmorDirectMembers[] = {
    {"armorRating", 0x200, ValueType::UInt32},  // 0 — TESObjectARMO::armorRating
    {"enchantment", 0x058, ValueType::FormRef}, // 1 — TESEnchantableForm(0x050) + formEnchanting(0x08)
};

// NPC-specific direct members
// Verified against CommonLibSSE-NG: TESNPC inherits TESActorBase which has
// TESActorBaseData at 0x030 with ACTOR_BASE_DATA at +0x08
inline constexpr ComponentMember kNpcDirectMembers[] = {
    {"level",        0x040, ValueType::UInt16},  // 0 — TESActorBaseData(0x030)+actorData(0x08)+level(0x08)
    {"calcLevelMin", 0x042, ValueType::UInt16},  // 1 — +calcLevelMin(0x0A)
    {"calcLevelMax", 0x044, ValueType::UInt16},  // 2 — +calcLevelMax(0x0C)
    {"speedMult",    0x046, ValueType::UInt16},  // 3 — +speedMult(0x0E)
    {"flags",        0x038, ValueType::UInt32},  // 4 — TESActorBaseData(0x030)+actorData(0x08)+flags(0x00)
    {"raceForm",     0x158, ValueType::FormRef}, // 5 — TESRaceForm(0x150)+race(0x08)
    {"classForm",    0x1C0, ValueType::FormRef}, // 6 — TESNPC::npcClass
    {"voiceType",    0x058, ValueType::FormRef}, // 7 — TESActorBaseData(0x030)+voiceType(0x28)
    {"skinForm",     0x108, ValueType::FormRef}, // 8 — BGSSkinForm(0x100)+skin(0x08)
    {"outfitForm",   0x218, ValueType::FormRef}, // 9 — TESNPC::defaultOutfit
};

// NPC form-array components (spells, perks, factions, shouts)
// These use RT functions, not GEP+store
inline constexpr ComponentMember kSpellListMembers[] = {
    {"spells", 0x0A0, ValueType::FormRef},
};

inline constexpr ComponentMember kPerkArrayMembers[] = {
    {"perks", 0x138, ValueType::FormRef},
};

inline constexpr ComponentMember kFactionListMembers[] = {
    {"factions", 0x070, ValueType::FormRef},
};

inline constexpr ComponentMember kShoutListMembers[] = {
    {"shouts", 0x0A0, ValueType::FormRef}, // via TESSpellList
};

// Leveled list component (relative to TESLeveledList component at +0x030)
inline constexpr ComponentMember kLeveledListMembers[] = {
    {"entries",     0x08, ValueType::FormRef},  // SimpleArray<LEVELED_OBJECT>*
    {"chanceNone",  0x10, ValueType::Int8},
    {"llFlags",     0x11, ValueType::UInt8},
    {"numEntries",  0x12, ValueType::UInt8},
    {"chanceGlobal",0x20, ValueType::FormRef},  // TESGlobal*
};

// ── Component definitions (index = component_idx) ──────────────────────

enum ComponentIdx : uint8_t {
    kCompValueForm = 0,
    kCompWeightForm,
    kCompAttackDamageForm,
    kCompFullName,
    kCompKeywordForm,
    kCompEnchantableForm,
    kCompWeaponDirect,
    kCompArmorDirect,
    kCompNpcDirect,
    kCompSpellList,
    kCompPerkArray,
    kCompFactionList,
    kCompShoutList,
    kCompLeveledList,
    kComponentCount,
};

inline constexpr ComponentDef kComponents[] = {
    {"TESValueForm",        kValueFormMembers,         1, ComponentDef::Kind::Scalar},     // 0
    {"TESWeightForm",       kWeightFormMembers,        1, ComponentDef::Kind::Scalar},     // 1
    {"TESAttackDamageForm", kAttackDamageFormMembers,  1, ComponentDef::Kind::Scalar},     // 2
    {"TESFullName",         kFullNameMembers,          1, ComponentDef::Kind::String},     // 3
    {"BGSKeywordForm",      kKeywordFormMembers,       2, ComponentDef::Kind::FormArray},  // 4
    {"TESEnchantableForm",  kEnchantableFormMembers,   1, ComponentDef::Kind::Scalar},     // 5
    {"WeaponDirect",        kWeaponDirectMembers,      6, ComponentDef::Kind::Scalar},     // 6
    {"ArmorDirect",         kArmorDirectMembers,       2, ComponentDef::Kind::Scalar},     // 7
    {"NpcDirect",           kNpcDirectMembers,        10, ComponentDef::Kind::Scalar},     // 8
    {"TESSpellList",        kSpellListMembers,         1, ComponentDef::Kind::FormArray},  // 9
    {"BGSPerkRankArray",    kPerkArrayMembers,         1, ComponentDef::Kind::FormArray},  // 10
    {"FactionList",         kFactionListMembers,       1, ComponentDef::Kind::FormArray},  // 11
    {"ShoutList",           kShoutListMembers,         1, ComponentDef::Kind::FormArray},  // 12
    {"TESLeveledList",      kLeveledListMembers,       5, ComponentDef::Kind::Scalar},     // 13
};

static_assert(sizeof(kComponents) / sizeof(kComponents[0]) == kComponentCount);

// ── Component slots within form types ──────────────────────────────────
// (component_idx, form_offset). For "Direct" pseudo-components,
// form_offset is 0 since member offsets are absolute.

struct ComponentSlot {
    uint8_t  component_idx;
    uint16_t form_offset;     // offset of this component within the form struct
};

inline constexpr ComponentSlot kWeaponSlots[] = {
    {kCompFullName,          0x030},
    {kCompEnchantableForm,   0x088},
    {kCompValueForm,         0x0A0},
    {kCompWeightForm,        0x0B0},
    {kCompAttackDamageForm,  0x0C0},
    {kCompKeywordForm,       0x140},
    {kCompWeaponDirect,      0x000},  // absolute offsets
};

inline constexpr ComponentSlot kArmorSlots[] = {
    {kCompFullName,          0x030},
    {kCompEnchantableForm,   0x050},
    {kCompValueForm,         0x068},
    {kCompWeightForm,        0x078},
    {kCompKeywordForm,       0x1D8},
    {kCompArmorDirect,       0x000},  // absolute offsets
};

inline constexpr ComponentSlot kNpcSlots[] = {
    {kCompFullName,          0x0D8},
    {kCompKeywordForm,       0x110},
    {kCompNpcDirect,         0x000},  // absolute offsets
    {kCompSpellList,         0x000},  // NPC spell list (absolute)
    {kCompPerkArray,         0x000},  // NPC perk array (absolute)
    {kCompFactionList,       0x000},  // NPC factions (absolute)
    {kCompShoutList,         0x000},  // NPC shouts via TESSpellList (absolute)
};

inline constexpr ComponentSlot kLeveledItemSlots[] = {
    {kCompLeveledList,       0x030},  // TESLeveledList component
};

inline constexpr ComponentSlot kLeveledCharSlots[] = {
    {kCompLeveledList,       0x030},  // TESLeveledList component
};

// ── Form type definitions ──────────────────────────────────────────────

struct FormTypeDef {
    const char*          record_tag;      // "WEAP", "ARMO", "NPC_"
    const char*          relation_name;   // "weapon", "armor", "npc"
    uint8_t              form_type_byte;  // 0x29, 0x1A, 0x2B
    TypeKind             type_kind;       // TypeKind::WeaponID, etc.
    const ComponentSlot* slots;
    uint8_t              slot_count;
};

inline constexpr FormTypeDef kWeapon = {
    "WEAP", "weapon", 0x29, TypeKind::WeaponID,
    kWeaponSlots, sizeof(kWeaponSlots) / sizeof(kWeaponSlots[0]),
};

inline constexpr FormTypeDef kArmor = {
    "ARMO", "armor", 0x1A, TypeKind::ArmorID,
    kArmorSlots, sizeof(kArmorSlots) / sizeof(kArmorSlots[0]),
};

inline constexpr FormTypeDef kNpc = {
    "NPC_", "npc", 0x2B, TypeKind::NpcID,
    kNpcSlots, sizeof(kNpcSlots) / sizeof(kNpcSlots[0]),
};

inline constexpr FormTypeDef kLeveledItem = {
    "LVLI", "leveled_list", 0x2D, TypeKind::FormID,
    kLeveledItemSlots, sizeof(kLeveledItemSlots) / sizeof(kLeveledItemSlots[0]),
};

inline constexpr FormTypeDef kLeveledChar = {
    "LVLN", "leveled_char", 0x2C, TypeKind::FormID,
    kLeveledCharSlots, sizeof(kLeveledCharSlots) / sizeof(kLeveledCharSlots[0]),
};

// Master form type array (all form types that have modifiable components)
inline constexpr const FormTypeDef* kFormTypes[] = {
    &kWeapon, &kArmor, &kNpc, &kLeveledItem, &kLeveledChar,
};
inline constexpr size_t kFormTypeCount = sizeof(kFormTypes) / sizeof(kFormTypes[0]);

// ── Form type byte constants ───────────────────────────────────────────
// (replaces form_type:: namespace from form_constants.h)

namespace form_type_byte {
    inline constexpr uint8_t kKeyword     = 0x04;
    inline constexpr uint8_t kFaction     = 0x06;
    inline constexpr uint8_t kRace        = 0x0A;
    inline constexpr uint8_t kSpell       = 0x16;
    inline constexpr uint8_t kArmor       = 0x1A;
    inline constexpr uint8_t kWeapon      = 0x29;
    inline constexpr uint8_t kNPC         = 0x2B;
    inline constexpr uint8_t kLeveledChar = 0x2C;
    inline constexpr uint8_t kLeveledItem = 0x2D;
    inline constexpr uint8_t kPerk        = 0x31;
} // namespace form_type_byte

inline constexpr uint64_t kFormTypeOffset = 0x1A;  // offset of formType byte in TESForm

// ── Lookup functions ───────────────────────────────────────────────────

// Find the FormTypeDef for a given form type byte. Returns nullptr if unknown.
constexpr const FormTypeDef* find_form_type(uint8_t form_type_byte) {
    for (size_t i = 0; i < kFormTypeCount; i++) {
        if (kFormTypes[i]->form_type_byte == form_type_byte)
            return kFormTypes[i];
    }
    return nullptr;
}

// Find the component slot for a given component within a form type.
// Returns nullptr if the form type doesn't have this component.
constexpr const ComponentSlot* find_slot(const FormTypeDef& ft, uint8_t comp_idx) {
    for (uint8_t i = 0; i < ft.slot_count; i++) {
        if (ft.slots[i].component_idx == comp_idx)
            return &ft.slots[i];
    }
    return nullptr;
}

// Check if a form type has a given component.
constexpr bool has_component(const FormTypeDef& ft, uint8_t comp_idx) {
    return find_slot(ft, comp_idx) != nullptr;
}

// Resolve the byte offset of a component member within a form type.
// Returns 0 if the form type doesn't have the component.
constexpr uint64_t resolve_offset(const FormTypeDef& ft,
                                   uint8_t comp_idx, uint8_t member_idx) {
    auto* slot = find_slot(ft, comp_idx);
    if (!slot) return 0;
    return slot->form_offset + kComponents[comp_idx].members[member_idx].offset;
}

// Runtime version: look up FormTypeDef by form_type byte, then resolve.
inline uint64_t resolve_offset_dynamic(uint8_t form_type_byte,
                                        uint8_t comp_idx, uint8_t member_idx) {
    auto* ft = find_form_type(form_type_byte);
    if (!ft) return 0;
    return resolve_offset(*ft, comp_idx, member_idx);
}

// ── Field definitions (scalar operations) ──────────────────────────────
// Maps Datalog relation names and .mora action names to component members.

struct FieldDef {
    FieldId     field_id;
    const char* relation_name;   // "gold_value" — the Datalog relation
    const char* set_action;      // "set_gold_value" — the .mora action (nullptr if none)
    uint8_t     component_idx;   // index into kComponents[]
    uint8_t     member_idx;      // index into component's members[]
};

inline constexpr FieldDef kFields[] = {
    // Shared component fields (polymorphic across form types)
    {FieldId::GoldValue,   "gold_value",    "set_gold_value",    kCompValueForm,        0},
    {FieldId::Weight,      "weight",        "set_weight",        kCompWeightForm,       0},
    {FieldId::Damage,      "damage",        "set_damage",        kCompAttackDamageForm, 0},
    {FieldId::Name,        "name",          "set_name",          kCompFullName,         0},

    // Weapon-specific fields
    {FieldId::Speed,       "speed",         "set_speed",         kCompWeaponDirect, 0},
    {FieldId::Reach,       "reach",         "set_reach",         kCompWeaponDirect, 1},
    {FieldId::RangeMin,    "range_min",     "set_range_min",     kCompWeaponDirect, 2},
    {FieldId::RangeMax,    "range_max",     "set_range_max",     kCompWeaponDirect, 3},
    {FieldId::Stagger,     "stagger",       "set_stagger",       kCompWeaponDirect, 4},
    {FieldId::CritDamage,  "crit_damage",   "set_crit_damage",   kCompWeaponDirect, 5},

    // Enchantment — polymorphic via TESEnchantableForm (weapon + armor)
    {FieldId::EnchantmentForm, nullptr,     "set_enchantment",   kCompEnchantableForm, 0},

    // Armor-specific fields
    {FieldId::ArmorRating, "armor_rating",  "set_armor_rating",  kCompArmorDirect, 0},

    // NPC-specific scalar fields
    {FieldId::Level,       "base_level",    "set_level",         kCompNpcDirect, 0},
    {FieldId::CalcLevelMin,"calc_level_min","set_calc_level_min",kCompNpcDirect, 1},
    {FieldId::CalcLevelMax,"calc_level_max","set_calc_level_max",kCompNpcDirect, 2},
    {FieldId::SpeedMult,   "speed_mult",    "set_speed_mult",    kCompNpcDirect, 3},
    {FieldId::RaceForm,    "race_of",       "set_race",          kCompNpcDirect, 5},
    {FieldId::ClassForm,   nullptr,         "set_class",         kCompNpcDirect, 6},
    {FieldId::VoiceTypeForm,nullptr,        "set_voice_type",    kCompNpcDirect, 7},
    {FieldId::SkinForm,    nullptr,         "set_skin",          kCompNpcDirect, 8},
    {FieldId::OutfitForm,  nullptr,         "set_outfit",        kCompNpcDirect, 9},

    // Leveled list scalar fields
    {FieldId::ChanceNone,  nullptr,         "set_chance_none",   kCompLeveledList, 1},
};

inline constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

// ── Form array definitions (add/remove operations) ─────────────────────
// Maps Datalog relations and actions to RT functions for collection mutations.

struct FormArrayDef {
    FieldId     field_id;
    const char* relation_name;   // "has_keyword"
    const char* add_action;      // "add_keyword"
    const char* remove_action;   // "remove_keyword" (nullptr if remove not supported)
    uint8_t     component_idx;
    const char* rt_add_fn;       // "mora_rt_add_keyword"
    const char* rt_remove_fn;    // "mora_rt_remove_keyword" (nullptr if N/A)
    // ESP source info for reading
    const char* subrecord_tag;   // "KWDA"
    uint16_t    element_size;    // 4 (bytes per element in ESP)
    enum class EspKind : uint8_t { ArrayField, ListField } esp_kind;
};

inline constexpr FormArrayDef kFormArrays[] = {
    {FieldId::Keywords, "has_keyword", "add_keyword", "remove_keyword",
     kCompKeywordForm, "mora_rt_add_keyword", "mora_rt_remove_keyword",
     "KWDA", 4, FormArrayDef::EspKind::ArrayField},

    {FieldId::Spells, "has_spell", "add_spell", "remove_spell",
     kCompSpellList, "mora_rt_add_spell", "mora_rt_remove_spell",
     "SPLO", 4, FormArrayDef::EspKind::ListField},

    {FieldId::Perks, "has_perk", "add_perk", nullptr,
     kCompPerkArray, "mora_rt_add_perk", nullptr,
     "PRKR", 8, FormArrayDef::EspKind::ListField},

    {FieldId::Factions, "has_faction", "add_faction", "remove_faction",
     kCompFactionList, "mora_rt_add_faction", nullptr,
     "SNAM", 8, FormArrayDef::EspKind::ListField},

    {FieldId::Shouts, nullptr, "add_shout", "remove_shout",
     kCompShoutList, "mora_rt_add_shout", nullptr,
     nullptr, 0, FormArrayDef::EspKind::ListField},
};

inline constexpr size_t kFormArrayCount = sizeof(kFormArrays) / sizeof(kFormArrays[0]);

// ── ESP field source mapping ───────────────────────────────────────────
// Maps scalar fields to their ESP subrecord locations.
// The schema_registry uses this to auto-register Datalog relations.

struct EspFieldSource {
    uint8_t     field_idx;       // index into kFields[]
    const char* record_tag;      // "WEAP", "ARMO", "NPC_"
    const char* subrecord_tag;   // "DATA", "DNAM", "ACBS"
    uint16_t    esp_offset;      // byte offset within subrecord
    ValueType   read_type;       // how to read from ESP
};

inline constexpr EspFieldSource kEspSources[] = {
    // gold_value: both WEAP and ARMO (polymorphic via TESValueForm)
    {0, "WEAP", "DATA", 0,  ValueType::Int32},
    {0, "ARMO", "DATA", 0,  ValueType::Int32},

    // weight: both WEAP and ARMO
    {1, "WEAP", "DATA", 4,  ValueType::Float32},
    {1, "ARMO", "DATA", 4,  ValueType::Float32},

    // damage: WEAP only
    {2, "WEAP", "DATA", 8,  ValueType::Int16},

    // speed, reach, rangeMin, rangeMax, stagger, critDamage: WEAP only
    // (these are in the weapon's DNAM subrecord, not DATA)
    // Note: speed/reach/etc. are currently read from in-memory layout,
    // ESP sources can be added later when needed for Datalog queries

    // armor_rating: ARMO only
    {11, "ARMO", "DNAM", 0, ValueType::Float32},

    // base_level: NPC_ via ACBS
    {13, "NPC_", "ACBS", 8, ValueType::Int16},
};

inline constexpr size_t kEspSourceCount = sizeof(kEspSources) / sizeof(kEspSources[0]);

// ── Boolean flag definitions ───────────────────────────────────────────

struct FlagDef {
    FieldId     field_id;
    const char* set_action;      // "set_essential"
    uint8_t     component_idx;   // component with the flags word
    uint8_t     member_idx;      // member index of the flags word
    uint32_t    flag_bit;        // bit mask
};

inline constexpr FlagDef kFlags[] = {
    {FieldId::Essential,    "set_essential",      kCompNpcDirect, 4, 1u << 1},
    {FieldId::Protected,    "set_protected",      kCompNpcDirect, 4, 1u << 11},
    {FieldId::AutoCalcStats,"set_auto_calc_stats",kCompNpcDirect, 4, 1u << 4},
};

inline constexpr size_t kFlagCount = sizeof(kFlags) / sizeof(kFlags[0]);

// ── Convenience lookups ────────────────────────────────────────────────

// Find a FieldDef by FieldId.
constexpr const FieldDef* find_field(FieldId id) {
    for (size_t i = 0; i < kFieldCount; i++) {
        if (kFields[i].field_id == id) return &kFields[i];
    }
    return nullptr;
}

// constexpr string comparison helper
constexpr bool str_eq(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// Find a FieldDef by its set_action name.
constexpr const FieldDef* find_field_by_action(const char* action) {
    if (!action) return nullptr;
    for (size_t i = 0; i < kFieldCount; i++) {
        if (kFields[i].set_action && str_eq(kFields[i].set_action, action))
            return &kFields[i];
    }
    return nullptr;
}

// Find a FormArrayDef by FieldId.
constexpr const FormArrayDef* find_form_array(FieldId id) {
    for (size_t i = 0; i < kFormArrayCount; i++) {
        if (kFormArrays[i].field_id == id) return &kFormArrays[i];
    }
    return nullptr;
}

// Find a FlagDef by FieldId.
constexpr const FlagDef* find_flag(FieldId id) {
    for (size_t i = 0; i < kFlagCount; i++) {
        if (kFlags[i].field_id == id) return &kFlags[i];
    }
    return nullptr;
}

// Get the ValueType for a field from the model.
constexpr ValueType get_value_type(FieldId id) {
    auto* f = find_field(id);
    if (!f) return ValueType::Int32; // fallback
    return kComponents[f->component_idx].members[f->member_idx].value_type;
}

// Resolve a field's byte offset for a given form type byte.
// Convenience wrapper combining find_field + resolve_offset_dynamic.
inline uint64_t field_offset_for(uint8_t form_type_byte, FieldId id) {
    auto* f = find_field(id);
    if (!f) return 0;
    return resolve_offset_dynamic(form_type_byte, f->component_idx, f->member_idx);
}

// Count how many form types have a given component.
// Used by IR emitter to decide if runtime type dispatch is needed.
constexpr uint8_t count_form_types_with(uint8_t comp_idx) {
    uint8_t count = 0;
    for (size_t i = 0; i < kFormTypeCount; i++) {
        if (has_component(*kFormTypes[i], comp_idx)) count++;
    }
    return count;
}

// Return the single form type that has a component, or nullptr if multiple do.
constexpr const FormTypeDef* unique_form_type_for(uint8_t comp_idx) {
    const FormTypeDef* result = nullptr;
    for (size_t i = 0; i < kFormTypeCount; i++) {
        if (has_component(*kFormTypes[i], comp_idx)) {
            if (result) return nullptr;  // multiple form types -> ambiguous
            result = kFormTypes[i];
        }
    }
    return result;
}

// ── Existence-only form types ──────────────────────────────────────────
// Form types that only need existence relations (no modifiable components).
// Used by schema_registry for the existence relation loop.

struct ExistenceOnlyDef {
    const char* record_tag;
    const char* relation_name;
    TypeKind    type_kind;
};

inline constexpr ExistenceOnlyDef kExistenceOnly[] = {
    {"AMMO", "ammo",         TypeKind::FormID},
    {"ALCH", "potion",       TypeKind::FormID},
    {"INGR", "ingredient",   TypeKind::FormID},
    {"BOOK", "book",         TypeKind::FormID},
    {"SCRL", "scroll",       TypeKind::FormID},
    {"ENCH", "enchantment",  TypeKind::FormID},
    {"MGEF", "magic_effect", TypeKind::FormID},
    {"MISC", "misc_item",    TypeKind::FormID},
    {"SLGM", "soul_gem",     TypeKind::FormID},
    {"SPEL", "spell",        TypeKind::SpellID},
    {"PERK", "perk",         TypeKind::PerkID},
    {"KYWD", "keyword",      TypeKind::KeywordID},
    {"FACT", "faction",      TypeKind::FactionID},
    {"RACE", "race",         TypeKind::RaceID},
};

inline constexpr size_t kExistenceOnlyCount =
    sizeof(kExistenceOnly) / sizeof(kExistenceOnly[0]);

// ── Record types that have keywords (for has_keyword ESP sources) ──────

inline constexpr const char* kKeywordRecords[] = {
    "NPC_", "WEAP", "ARMO", "ALCH", "BOOK", "AMMO",
    "CONT", "MGEF", "INGR", "SCRL", "MISC", "SLGM",
};

inline constexpr size_t kKeywordRecordCount =
    sizeof(kKeywordRecords) / sizeof(kKeywordRecords[0]);

// ── Record types that have EDID (editor_id) ────────────────────────────

inline constexpr const char* kEditorIdRecords[] = {
    "NPC_", "WEAP", "ARMO", "SPEL", "PERK", "KYWD", "FACT", "RACE",
    "LVLI", "ALCH", "BOOK", "AMMO", "CONT", "MGEF",
};

inline constexpr size_t kEditorIdRecordCount =
    sizeof(kEditorIdRecords) / sizeof(kEditorIdRecords[0]);

// ── Record types that have FULL name ───────────────────────────────────

inline constexpr const char* kFullNameRecords[] = {
    "NPC_", "WEAP", "ARMO", "ALCH", "BOOK", "AMMO",
    "INGR", "SCRL", "ENCH", "MGEF",
};

inline constexpr size_t kFullNameRecordCount =
    sizeof(kFullNameRecords) / sizeof(kFullNameRecords[0]);

// ── Type system queries (for type checker) ─────────────────────────────

// Find the FormTypeDef for a given TypeKind. Returns nullptr for generic FormID.
constexpr const FormTypeDef* find_form_type_by_kind(TypeKind tk) {
    for (size_t i = 0; i < kFormTypeCount; i++) {
        if (kFormTypes[i]->type_kind == tk)
            return kFormTypes[i];
    }
    return nullptr;
}

// Check if a TypeKind's form type has a given component.
// Returns true for generic FormID (can't statically disprove).
constexpr bool type_has_component(TypeKind tk, uint8_t comp_idx) {
    if (tk == TypeKind::FormID) return true;
    auto* ft = find_form_type_by_kind(tk);
    if (!ft) return true;  // unknown type — allow
    return has_component(*ft, comp_idx);
}

// Convert model ValueType to TypeKind for type checking.
constexpr TypeKind value_type_to_type_kind(ValueType vt) {
    switch (vt) {
        case ValueType::Int8:
        case ValueType::Int16:
        case ValueType::Int32:
        case ValueType::UInt8:
        case ValueType::UInt16:
        case ValueType::UInt32:    return TypeKind::Int;
        case ValueType::Float32:   return TypeKind::Float;
        case ValueType::FormRef:   return TypeKind::FormID;
        case ValueType::BSFixedString: return TypeKind::String;
    }
    return TypeKind::Int;
}

// Determine the narrowest first-param TypeKind for an effect's component.
constexpr TypeKind effect_form_type_kind(uint8_t comp_idx) {
    auto* unique = unique_form_type_for(comp_idx);
    return unique ? unique->type_kind : TypeKind::FormID;
}

// Get all form type names that have a given component (for error messages).
inline std::string form_types_with_component(uint8_t comp_idx) {
    std::string result;
    for (size_t i = 0; i < kFormTypeCount; i++) {
        if (has_component(*kFormTypes[i], comp_idx)) {
            if (!result.empty()) result += ", ";
            result += kFormTypes[i]->relation_name;
        }
    }
    return result;
}

} // namespace mora::model
