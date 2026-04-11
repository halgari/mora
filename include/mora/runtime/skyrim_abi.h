#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// Mora Skyrim ABI Shim
// ═══════════════════════════════════════════════════════════════════════════
//
// Minimal struct definitions matching Skyrim SE's in-memory form layouts.
// Offsets verified against powerof3's CommonLibSSE (static_assert'd sizes).
// This replaces the full CommonLibSSE dependency for the Mora runtime.
//
// These layouts are the stable ABI contract between Mora and Skyrim.
// When Mora eventually emits LLVM IR → DLL, these same definitions
// become the target struct layouts for generated code.
//
// Offset notation: // 0xNN = byte offset from start of THIS base class
//                  // @0xNN = absolute offset from start of derived class
//
// ═══════════════════════════════════════════════════════════════════════════

#ifdef _WIN32

#include <cstdint>
#include <cstddef>

namespace skyrim {

// ── Forward declarations ────────────────────────────────────────────────

struct TESForm;
struct BGSKeyword;
struct TESFaction;
struct SpellItem;
struct BGSPerk;
struct TESRace;
struct TESObjectARMA;

// ── Form types enum (subset) ────────────────────────────────────────────

enum class FormType : uint8_t {
    Keyword  = 0x04,
    Faction  = 0x06,
    Race     = 0x0A,
    Spell    = 0x16,
    NPC      = 0x2B,
    Weapon   = 0x29,
    Armor    = 0x1A,
    Perk     = 0x31,
};

// ── TESForm (0x20 bytes) ────────────────────────────────────────────────
// Base of all game records. Every form in Skyrim inherits from this.

struct TESForm {
    void*       vtable;          // 0x00
    void*       sourceFiles;     // 0x08
    uint32_t    formFlags;       // 0x10
    uint32_t    formID;          // 0x14
    uint16_t    inGameFormFlags; // 0x18
    uint8_t     formType;        // 0x1A (FormType enum)
    uint8_t     pad1B;           // 0x1B
    uint32_t    pad1C;           // 0x1C
};
static_assert(sizeof(TESForm) == 0x20);

// ── Component base classes ──────────────────────────────────────────────
// These are mixed into derived types via multiple inheritance.
// Each has a vtable pointer + members.

// BaseFormComponent: just a vtable (0x08 bytes)
struct BaseFormComponent {
    void* vtable; // 0x00
};
static_assert(sizeof(BaseFormComponent) == 0x08);

// TESFullName: display name (0x10 bytes)
// Members at +0x08: BSFixedString fullName
struct TESFullName : BaseFormComponent {
    void* fullName; // 0x08 — BSFixedString (pointer to interned string data)
};
static_assert(sizeof(TESFullName) == 0x10);

// TESValueForm: gold value (0x10 bytes)
struct TESValueForm : BaseFormComponent {
    int32_t  value;  // 0x08
    uint32_t pad0C;  // 0x0C
};
static_assert(sizeof(TESValueForm) == 0x10);

// TESWeightForm: carry weight (0x10 bytes)
struct TESWeightForm : BaseFormComponent {
    float    weight; // 0x08
    uint32_t pad0C;  // 0x0C
};
static_assert(sizeof(TESWeightForm) == 0x10);

// TESAttackDamageForm: base damage (0x10 bytes)
struct TESAttackDamageForm : BaseFormComponent {
    uint16_t attackDamage; // 0x08
    uint16_t pad0A;        // 0x0A
    uint32_t pad0C;        // 0x0C
};
static_assert(sizeof(TESAttackDamageForm) == 0x10);

// BGSKeywordForm: keyword array (0x18 bytes)
struct BGSKeywordForm : BaseFormComponent {
    BGSKeyword** keywords;     // 0x08
    uint32_t     numKeywords;  // 0x10
    uint32_t     pad14;        // 0x14
};
static_assert(sizeof(BGSKeywordForm) == 0x18);

// FACTION_RANK: faction membership entry (0x10 bytes)
struct FACTION_RANK {
    TESFaction* faction;    // 0x00
    int8_t      rank;       // 0x08
    uint8_t     pad09;      // 0x09
    uint16_t    pad0A;      // 0x0A
    uint32_t    pad0C;      // 0x0C
};
static_assert(sizeof(FACTION_RANK) == 0x10);

// PerkRankData: perk + rank (0x10 bytes)
struct PerkRankData {
    BGSPerk* perk;         // 0x00
    int8_t   currentRank;  // 0x08
    uint8_t  pad09;        // 0x09
    uint16_t pad0A;        // 0x0A
    uint32_t pad0C;        // 0x0C
};
static_assert(sizeof(PerkRankData) == 0x10);

// BGSPerkRankArray: perk list (0x18 bytes)
struct BGSPerkRankArray : BaseFormComponent {
    PerkRankData* perks;      // 0x08
    uint32_t      perkCount;  // 0x10
    uint32_t      pad14;      // 0x14
};
static_assert(sizeof(BGSPerkRankArray) == 0x18);

// TESSpellList::SpellData (0x28 bytes)
struct SpellData {
    SpellItem** spells;        // 0x00
    void**      levSpells;     // 0x08
    void**      shouts;        // 0x10
    uint32_t    numSpells;     // 0x18
    uint32_t    numLevSpells;  // 0x1C
    uint32_t    numShouts;     // 0x20
    uint32_t    pad24;         // 0x24
};
static_assert(sizeof(SpellData) == 0x28);

// TESSpellList: spell list component (0x10 bytes)
struct TESSpellList : BaseFormComponent {
    SpellData* actorEffects; // 0x08
};
static_assert(sizeof(TESSpellList) == 0x10);

// BSTArray<T>: Bethesda's dynamic array (0x18 bytes)
// Layout: { T* data; uint32_t capacity; uint32_t pad; uint32_t size; uint32_t pad; }
template <typename T>
struct BSTArray {
    T*       data;      // 0x00
    uint32_t capacity;  // 0x08
    uint32_t pad0C;     // 0x0C
    uint32_t size;      // 0x10
    uint32_t pad14;     // 0x14
};

// TESActorBaseData (partial): faction array at offset 0x40 from start
// We access it via offset from TESNPC base

// ── BGSKeyword (0x28 bytes) ─────────────────────────────────────────────

struct BGSKeyword : TESForm {
    void* formEditorID; // 0x20 — BSFixedString
};
static_assert(sizeof(BGSKeyword) == 0x28);

// ── Derived form offset maps ────────────────────────────────────────────
// These encode the byte offset of each component within the derived class.
// Used by FormBridge to access the right base class for a given form.

namespace npc_offsets {
    // TESNPC inherits TESActorBase @ 0x000, which inherits:
    constexpr size_t keyword_form    = 0x110; // BGSKeywordForm
    constexpr size_t spell_list      = 0x0A0; // TESSpellList
    constexpr size_t full_name       = 0x0D8; // TESFullName
    constexpr size_t perk_array      = 0x138; // BGSPerkRankArray
    constexpr size_t actor_base_data = 0x030; // TESActorBaseData
    // Faction array is at TESActorBaseData + 0x40 (BSTArray<FACTION_RANK>)
    constexpr size_t factions        = 0x030 + 0x40;
    // TESRaceForm @ 0x150 from TESNPC
    constexpr size_t race_form       = 0x150;

    constexpr size_t total_size      = 0x268;
}

namespace weapon_offsets {
    constexpr size_t full_name       = 0x030; // TESFullName
    constexpr size_t value_form      = 0x0A0; // TESValueForm
    constexpr size_t weight_form     = 0x0B0; // TESWeightForm
    constexpr size_t attack_damage   = 0x0C0; // TESAttackDamageForm
    constexpr size_t keyword_form    = 0x140; // BGSKeywordForm

    constexpr size_t total_size      = 0x220;
}

namespace armor_offsets {
    constexpr size_t full_name       = 0x030; // TESFullName
    constexpr size_t value_form      = 0x068; // TESValueForm
    constexpr size_t weight_form     = 0x078; // TESWeightForm
    constexpr size_t keyword_form    = 0x1D8; // BGSKeywordForm
    constexpr size_t armor_rating    = 0x200; // uint32_t (CK value * 100)

    constexpr size_t total_size      = 0x228;
}

// ── Runtime access helpers ──────────────────────────────────────────────
// These cast raw form pointers to the right component at the right offset.

template <typename T>
inline T* component_at(void* form, size_t offset) {
    return reinterpret_cast<T*>(reinterpret_cast<char*>(form) + offset);
}

inline BGSKeywordForm* get_keyword_form(TESForm* form) {
    switch (static_cast<FormType>(form->formType)) {
        case FormType::NPC:    return component_at<BGSKeywordForm>(form, npc_offsets::keyword_form);
        case FormType::Weapon: return component_at<BGSKeywordForm>(form, weapon_offsets::keyword_form);
        case FormType::Armor:  return component_at<BGSKeywordForm>(form, armor_offsets::keyword_form);
        default: return nullptr;
    }
}

inline TESValueForm* get_value_form(TESForm* form) {
    switch (static_cast<FormType>(form->formType)) {
        case FormType::Weapon: return component_at<TESValueForm>(form, weapon_offsets::value_form);
        case FormType::Armor:  return component_at<TESValueForm>(form, armor_offsets::value_form);
        default: return nullptr;
    }
}

inline TESWeightForm* get_weight_form(TESForm* form) {
    switch (static_cast<FormType>(form->formType)) {
        case FormType::Weapon: return component_at<TESWeightForm>(form, weapon_offsets::weight_form);
        case FormType::Armor:  return component_at<TESWeightForm>(form, armor_offsets::weight_form);
        default: return nullptr;
    }
}

inline TESAttackDamageForm* get_attack_damage_form(TESForm* form) {
    if (static_cast<FormType>(form->formType) == FormType::Weapon)
        return component_at<TESAttackDamageForm>(form, weapon_offsets::attack_damage);
    return nullptr;
}

inline TESFullName* get_full_name(TESForm* form) {
    switch (static_cast<FormType>(form->formType)) {
        case FormType::NPC:    return component_at<TESFullName>(form, npc_offsets::full_name);
        case FormType::Weapon: return component_at<TESFullName>(form, weapon_offsets::full_name);
        case FormType::Armor:  return component_at<TESFullName>(form, armor_offsets::full_name);
        default: return nullptr;
    }
}

inline TESSpellList* get_spell_list(TESForm* form) {
    if (static_cast<FormType>(form->formType) == FormType::NPC)
        return component_at<TESSpellList>(form, npc_offsets::spell_list);
    return nullptr;
}

inline BGSPerkRankArray* get_perk_array(TESForm* form) {
    if (static_cast<FormType>(form->formType) == FormType::NPC)
        return component_at<BGSPerkRankArray>(form, npc_offsets::perk_array);
    return nullptr;
}

// ── SKSE Plugin API ─────────────────────────────────────────────────────
// Minimal declarations for SKSE plugin entry points.
// These match SKSE64's binary interface.

namespace skse {

struct PluginInfo {
    uint32_t    infoVersion;
    const char* name;
    uint32_t    version;
};

struct LoadInterface {
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    // QueryInterface function pointer at runtime
};

// Message types for MessagingInterface
enum MessageType : uint32_t {
    kPostLoad     = 0,
    kPostPostLoad = 1,
    kPreLoadGame  = 2,
    kPostLoadGame = 3,
    kSaveGame     = 4,
    kDeleteGame   = 5,
    kInputLoaded  = 6,
    kNewGame      = 7,
    kDataLoaded   = 8,
};

struct Message {
    const char* sender;
    uint32_t    type;
    uint32_t    dataLen;
    void*       data;
};

using EventCallback = void(*)(Message* msg);

} // namespace skse

// ── TESDataHandler singleton access ─────────────────────────────────────
// At runtime, we need to call into Skyrim's TESDataHandler to look up forms.
// These are resolved via Address Library at load time.
//
// For now, declare function pointer types. The plugin initialization
// will resolve actual addresses.

using LookupFormByID_t = TESForm* (*)(uint32_t formID);
using GetFormArraySize_t = uint32_t (*)(FormType type);

// Global function pointers — resolved at plugin load
extern LookupFormByID_t g_lookupFormByID;

} // namespace skyrim

#endif // _WIN32
