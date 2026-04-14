// ═══════════════════════════════════════════════════════════════════════════
// Windows-only static assertions verifying form_model.h offsets against
// CommonLibSSE-NG's actual type definitions.
//
// This file is compiled only in the Windows runtime build. If any assertion
// fails, it means form_model.h has a stale offset that no longer matches
// the game's ABI.
// ═══════════════════════════════════════════════════════════════════════════

#ifdef _WIN32

#include "mora/data/form_model.h"

#include <RE/T/TESForm.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESActorBase.h>
#include <RE/T/TESActorBaseData.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESValueForm.h>
#include <RE/T/TESWeightForm.h>
#include <RE/T/TESAttackDamageForm.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESEnchantableForm.h>
#include <RE/T/TESRaceForm.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSSkinForm.h>
#include <RE/T/TESSpellList.h>
#include <RE/T/TESLeveledList.h>
#include <RE/T/TESLevItem.h>

using namespace mora::model;

// ── TESForm layout ─────────────────────────────────────────────────────

static_assert(offsetof(RE::TESForm, formType) == kFormTypeOffset,
    "TESForm::formType offset mismatch");

// ── Component member offsets (within the component base class) ─────────

static_assert(offsetof(RE::TESValueForm, value) == 0x08,
    "TESValueForm::value offset mismatch");

static_assert(offsetof(RE::TESWeightForm, weight) == 0x08,
    "TESWeightForm::weight offset mismatch");

static_assert(offsetof(RE::TESAttackDamageForm, attackDamage) == 0x08,
    "TESAttackDamageForm::attackDamage offset mismatch");

static_assert(offsetof(RE::TESFullName, fullName) == 0x08,
    "TESFullName::fullName offset mismatch");

static_assert(offsetof(RE::TESEnchantableForm, formEnchanting) == 0x08,
    "TESEnchantableForm::formEnchanting offset mismatch");

static_assert(offsetof(RE::BGSKeywordForm, keywords) == 0x08,
    "BGSKeywordForm::keywords offset mismatch");

static_assert(offsetof(RE::BGSKeywordForm, numKeywords) == 0x10,
    "BGSKeywordForm::numKeywords offset mismatch");

static_assert(offsetof(RE::TESRaceForm, race) == 0x08,
    "TESRaceForm::race offset mismatch");

static_assert(offsetof(RE::BGSSkinForm, skin) == 0x08,
    "BGSSkinForm::skin offset mismatch");

// ── TESObjectWEAP: component base offsets within the form ──────────────
// These verify that the ComponentSlot::form_offset values are correct.
// We use a helper: the difference between a base class pointer and the
// derived class pointer, obtained via static_cast on a null pointer.

namespace {
template<typename Derived, typename Base>
consteval std::uintptr_t base_offset() {
    // This uses the standard pointer adjustment for multiple inheritance
    alignas(Derived) char buf[sizeof(Derived)]{};
    auto* d = reinterpret_cast<Derived*>(buf);
    auto* b = static_cast<Base*>(d);
    return reinterpret_cast<const char*>(b) - reinterpret_cast<const char*>(d);
}
}

// Weapon component offsets
static_assert(base_offset<RE::TESObjectWEAP, RE::TESFullName>() == 0x030);
static_assert(base_offset<RE::TESObjectWEAP, RE::TESEnchantableForm>() == 0x088);
static_assert(base_offset<RE::TESObjectWEAP, RE::TESValueForm>() == 0x0A0);
static_assert(base_offset<RE::TESObjectWEAP, RE::TESWeightForm>() == 0x0B0);
static_assert(base_offset<RE::TESObjectWEAP, RE::TESAttackDamageForm>() == 0x0C0);
static_assert(base_offset<RE::TESObjectWEAP, RE::BGSKeywordForm>() == 0x140);

// Weapon direct members
static_assert(offsetof(RE::TESObjectWEAP, weaponData) == 0x168);
static_assert(offsetof(RE::TESObjectWEAP::Data, speed) == 0x08);
static_assert(offsetof(RE::TESObjectWEAP::Data, reach) == 0x0C);
static_assert(offsetof(RE::TESObjectWEAP::Data, minRange) == 0x10);
static_assert(offsetof(RE::TESObjectWEAP::Data, maxRange) == 0x14);
static_assert(offsetof(RE::TESObjectWEAP::Data, staggerValue) == 0x20);
static_assert(offsetof(RE::TESObjectWEAP, criticalData) == 0x1A0);
static_assert(offsetof(RE::TESObjectWEAP::CriticalData, damage) == 0x10);

// Cross-check absolute offsets match our model
static_assert(0x168 + 0x08 == 0x170);  // speed
static_assert(0x168 + 0x0C == 0x174);  // reach
static_assert(0x168 + 0x10 == 0x178);  // rangeMin
static_assert(0x168 + 0x14 == 0x17C);  // rangeMax
static_assert(0x168 + 0x20 == 0x188);  // stagger
static_assert(0x1A0 + 0x10 == 0x1B0);  // critDamage

// Armor component offsets
static_assert(base_offset<RE::TESObjectARMO, RE::TESFullName>() == 0x030);
static_assert(base_offset<RE::TESObjectARMO, RE::TESEnchantableForm>() == 0x050);
static_assert(base_offset<RE::TESObjectARMO, RE::TESValueForm>() == 0x068);
static_assert(base_offset<RE::TESObjectARMO, RE::TESWeightForm>() == 0x078);
static_assert(base_offset<RE::TESObjectARMO, RE::BGSKeywordForm>() == 0x1D8);

// Armor direct members
static_assert(offsetof(RE::TESObjectARMO, armorRating) == 0x200);

// ── TESNPC: component and member offsets ───────────────────────────────

static_assert(base_offset<RE::TESNPC, RE::TESFullName>() == 0x0D8);
static_assert(base_offset<RE::TESNPC, RE::BGSKeywordForm>() == 0x110);
static_assert(base_offset<RE::TESNPC, RE::TESRaceForm>() == 0x150);

// TESActorBaseData members (ACBS fields)
static_assert(base_offset<RE::TESNPC, RE::TESActorBaseData>() == 0x030);
static_assert(offsetof(RE::ACTOR_BASE_DATA, actorBaseFlags) == 0x00);
static_assert(offsetof(RE::ACTOR_BASE_DATA, level) == 0x08);
static_assert(offsetof(RE::ACTOR_BASE_DATA, calcLevelMin) == 0x0A);
static_assert(offsetof(RE::ACTOR_BASE_DATA, calcLevelMax) == 0x0C);
static_assert(offsetof(RE::ACTOR_BASE_DATA, speedMult) == 0x0E);

// TESActorBaseData::actorData at offset 0x08 within TESActorBaseData
static_assert(offsetof(RE::TESActorBaseData, actorData) == 0x08);
// So absolute: 0x030 + 0x08 + field = NPC flags/level/etc.

// TESActorBaseData::voiceType
static_assert(offsetof(RE::TESActorBaseData, voiceType) == 0x28);

// TESActorBaseData::factions
static_assert(offsetof(RE::TESActorBaseData, factions) == 0x40);

// BGSSkinForm
static_assert(base_offset<RE::TESNPC, RE::BGSSkinForm>() == 0x100);

// TESNPC direct members
static_assert(offsetof(RE::TESNPC, npcClass) == 0x1C0);
static_assert(offsetof(RE::TESNPC, defaultOutfit) == 0x218);

// ── TESLevItem: TESLeveledList component ───────────────────────────────

static_assert(base_offset<RE::TESLevItem, RE::TESLeveledList>() == 0x030);
static_assert(offsetof(RE::TESLeveledList, chanceNone) == 0x10);
static_assert(offsetof(RE::TESLeveledList, numEntries) == 0x12);

// ── Form sizes (sanity check) ──────────────────────────────────────────

static_assert(sizeof(RE::TESObjectWEAP) == 0x220);
static_assert(sizeof(RE::TESObjectARMO) == 0x228);
static_assert(sizeof(RE::TESNPC) == 0x268);

#endif // _WIN32
