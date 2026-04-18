// ═══════════════════════════════════════════════════════════════════════════
// SKSEGameAPI — concrete GameAPI using CommonLibSSE-NG TESForm accessors.
//
// Dispatch is keyword-string driven (one string lookup per row at startup —
// the Skyrim DataLoaded event fires once per game session). Per-keyword
// logic distilled from master's src/rt/patch_walker.cpp; collapsed from the
// old FieldId-enum dispatch into a keyword → SDK-call mapping matching the
// strings produced by the Linux compile step's effect relations.
//
// Compiled only when MORA_WITH_COMMONLIB is defined (Windows DLL build).
// ═══════════════════════════════════════════════════════════════════════════

#ifdef MORA_WITH_COMMONLIB

#include "mora_skyrim_runtime/game_api.h"
#include "mora/core/string_pool.h"

#include <RE/T/TESForm.h>
#include <RE/F/FormTraits.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESActorBaseData.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSKeyword.h>
#include <RE/T/TESLevItem.h>
#include <RE/T/TESLevCharacter.h>
#include <RE/S/SpellItem.h>
#include <RE/B/BGSPerk.h>
#include <RE/T/TESFaction.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESValueForm.h>
#include <RE/T/TESWeightForm.h>
#include <RE/T/TESAttackDamageForm.h>
#include <RE/M/MemoryManager.h>
#include <SKSE/SKSE.h>

#include <cstdint>
#include <cstring>
#include <memory>

namespace mora_skyrim_runtime {

namespace {

// ── Component cast helper (form → component base class) ──────────────
// TESForm::As<T>() only works for concrete form types, not component
// base classes. Cast through the concrete form type instead.
template<typename ComponentT>
ComponentT* get_component(RE::TESForm* form) {
    if constexpr (std::is_base_of_v<ComponentT, RE::TESObjectWEAP>) {
        if (auto* w = form->As<RE::TESObjectWEAP>())
            return static_cast<ComponentT*>(w);
    }
    if constexpr (std::is_base_of_v<ComponentT, RE::TESObjectARMO>) {
        if (auto* a = form->As<RE::TESObjectARMO>())
            return static_cast<ComponentT*>(a);
    }
    if constexpr (std::is_base_of_v<ComponentT, RE::TESNPC>) {
        if (auto* n = form->As<RE::TESNPC>())
            return static_cast<ComponentT*>(n);
    }
    return nullptr;
}

RE::TESLeveledList* get_leveled_list(RE::TESForm* form) {
    if (auto* li = form->As<RE::TESLevItem>())
        return static_cast<RE::TESLeveledList*>(li);
    if (auto* lc = form->As<RE::TESLevCharacter>())
        return static_cast<RE::TESLeveledList*>(lc);
    return nullptr;
}

// ── Value coercion helpers ───────────────────────────────────────────
int64_t as_int_or_zero(const mora::Value& v) {
    switch (v.kind()) {
        case mora::Value::Kind::Int:    return v.as_int();
        case mora::Value::Kind::Float:  return static_cast<int64_t>(v.as_float());
        case mora::Value::Kind::FormID: return static_cast<int64_t>(v.as_formid());
        case mora::Value::Kind::Bool:   return v.as_bool() ? 1 : 0;
        default: return 0;
    }
}
double as_float_or_zero(const mora::Value& v) {
    switch (v.kind()) {
        case mora::Value::Kind::Float: return v.as_float();
        case mora::Value::Kind::Int:   return static_cast<double>(v.as_int());
        default: return 0.0;
    }
}
uint32_t as_formid_or_zero(const mora::Value& v) {
    switch (v.kind()) {
        case mora::Value::Kind::FormID: return v.as_formid();
        case mora::Value::Kind::Int:    return static_cast<uint32_t>(v.as_int());
        default: return 0;
    }
}

// ── Per-field setters (all op=Set) ───────────────────────────────────
void set_name(RE::TESForm* form, std::string_view name) {
    auto* nf = get_component<RE::TESFullName>(form);
    if (!nf) return;
    // CommonLib's BSFixedString takes char*; construct a null-terminated
    // copy (std::string_view isn't NTBS-guaranteed).
    std::string s(name);
    nf->fullName = s.c_str();
}

void set_gold_value(RE::TESForm* form, int64_t v) {
    auto* vf = get_component<RE::TESValueForm>(form);
    if (vf) vf->value = static_cast<int32_t>(v);
}

void set_weight(RE::TESForm* form, double v) {
    auto* wf = get_component<RE::TESWeightForm>(form);
    if (wf) wf->weight = static_cast<float>(v);
}

void set_damage(RE::TESForm* form, int64_t v) {
    auto* weap = form->As<RE::TESObjectWEAP>();
    if (!weap) return;
    auto* df = static_cast<RE::TESAttackDamageForm*>(weap);
    df->attackDamage = static_cast<uint16_t>(v);
}

void set_armor_rating(RE::TESForm* form, int64_t v) {
    auto* a = form->As<RE::TESObjectARMO>();
    if (a) a->armorRating = static_cast<uint32_t>(v);
}

void set_weapon_speed(RE::TESForm* form, double v) {
    auto* w = form->As<RE::TESObjectWEAP>();
    if (w) w->weaponData.speed = static_cast<float>(v);
}

void set_weapon_reach(RE::TESForm* form, double v) {
    auto* w = form->As<RE::TESObjectWEAP>();
    if (w) w->weaponData.reach = static_cast<float>(v);
}

void set_npc_base_level(RE::TESForm* form, int64_t v) {
    auto* n = form->As<RE::TESNPC>();
    if (n) n->actorData.level = static_cast<uint16_t>(v);
}

void set_npc_calc_level_min(RE::TESForm* form, int64_t v) {
    auto* n = form->As<RE::TESNPC>();
    if (n) n->actorData.calcLevelMin = static_cast<uint16_t>(v);
}

void set_npc_calc_level_max(RE::TESForm* form, int64_t v) {
    auto* n = form->As<RE::TESNPC>();
    if (n) n->actorData.calcLevelMax = static_cast<uint16_t>(v);
}

void set_npc_speed_mult(RE::TESForm* form, int64_t v) {
    auto* n = form->As<RE::TESNPC>();
    if (n) n->actorData.speedMult = static_cast<uint16_t>(v);
}

void set_chance_none(RE::TESForm* form, int64_t v) {
    auto* ll = get_leveled_list(form);
    if (ll) ll->chanceNone = static_cast<int8_t>(v);
}

// ── Keyword add / remove ─────────────────────────────────────────────
void add_keyword(RE::TESForm* form, uint32_t kw_formid) {
    auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(kw_formid);
    if (!kw) return;
    auto* kf = get_component<RE::BGSKeywordForm>(form);
    if (!kf) return;
    if (!kf->HasKeyword(kw)) kf->AddKeyword(kw);
}

void remove_keyword(RE::TESForm* form, uint32_t kw_formid) {
    auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(kw_formid);
    if (!kw) return;
    auto* kf = get_component<RE::BGSKeywordForm>(form);
    if (kf) kf->RemoveKeyword(kw);
}

// ── NPC spell / perk / faction adds ──────────────────────────────────
void add_spell(RE::TESForm* form, uint32_t sp_formid) {
    auto* npc   = form->As<RE::TESNPC>();
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(sp_formid);
    if (!npc || !spell) return;
    auto* data = npc->GetSpellList();
    if (!data) return;
    for (uint32_t i = 0; i < data->numSpells; ++i)
        if (data->spells[i] == spell) return;

    uint32_t const newCount = data->numSpells + 1;
    auto* fresh = static_cast<RE::SpellItem**>(
        RE::malloc(newCount * sizeof(RE::SpellItem*)));
    if (!fresh) return;
    if (data->spells && data->numSpells > 0)
        std::memcpy(fresh, data->spells, data->numSpells * sizeof(RE::SpellItem*));
    fresh[data->numSpells] = spell;
    RE::free(data->spells);
    data->spells = fresh;
    data->numSpells = newCount;
}

void add_perk(RE::TESForm* form, uint32_t perk_formid) {
    auto* npc  = form->As<RE::TESNPC>();
    auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(perk_formid);
    if (!npc || !perk) return;
    for (uint32_t i = 0; i < npc->perkCount; ++i)
        if (npc->perks[i].perk == perk) return;

    uint32_t const newCount = npc->perkCount + 1;
    auto* fresh = static_cast<RE::PerkRankData*>(
        RE::malloc(newCount * sizeof(RE::PerkRankData)));
    if (!fresh) return;
    if (npc->perkCount > 0)
        std::memcpy(fresh, &npc->perks[0], npc->perkCount * sizeof(RE::PerkRankData));
    fresh[npc->perkCount] = {perk, 0};
    RE::free(&npc->perks[0]);
    npc->perks = fresh;
    npc->perkCount = newCount;
}

void add_faction(RE::TESForm* form, uint32_t fac_formid) {
    auto* npc = form->As<RE::TESNPC>();
    auto* fac = RE::TESForm::LookupByID<RE::TESFaction>(fac_formid);
    if (!npc || !fac) return;
    for (auto& fr : npc->factions)
        if (fr.faction == fac) return;
    npc->factions.push_back({fac, 0});
}

// ── Keyword string comparison (constexpr-hash avoids strcmp cost) ────
bool eq(std::string_view a, std::string_view b) { return a == b; }

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// SKSEGameAPI
// ═══════════════════════════════════════════════════════════════════════════

class SKSEGameAPI final : public GameAPI {
public:
    explicit SKSEGameAPI(const mora::StringPool& pool) : pool_(pool) {}
    void set     (uint32_t target, std::string_view field, const mora::Value& v) override;
    void add     (uint32_t target, std::string_view field, const mora::Value& v) override;
    void remove  (uint32_t target, std::string_view field, const mora::Value& v) override;
    void multiply(uint32_t target, std::string_view field, const mora::Value& v) override;
private:
    const mora::StringPool& pool_;
};

void SKSEGameAPI::set(uint32_t target, std::string_view field,
                       const mora::Value& v)
{
    auto* form = RE::TESForm::LookupByID(target);
    if (!form) return;

    if (eq(field, "Name")) {
        if (v.kind() == mora::Value::Kind::String) {
            set_name(form, pool_.get(v.as_string()));
        } else if (v.kind() == mora::Value::Kind::Keyword) {
            set_name(form, pool_.get(v.as_keyword()));
        }
        return;
    }
    if (eq(field, "GoldValue"))    { set_gold_value(form, as_int_or_zero(v)); return; }
    if (eq(field, "Weight"))       { set_weight(form, as_float_or_zero(v)); return; }
    if (eq(field, "Damage"))       { set_damage(form, as_int_or_zero(v)); return; }
    if (eq(field, "ArmorRating"))  { set_armor_rating(form, as_int_or_zero(v)); return; }
    if (eq(field, "Speed"))        { set_weapon_speed(form, as_float_or_zero(v)); return; }
    if (eq(field, "Reach"))        { set_weapon_reach(form, as_float_or_zero(v)); return; }
    if (eq(field, "BaseLevel"))    { set_npc_base_level(form, as_int_or_zero(v)); return; }
    if (eq(field, "CalcLevelMin")) { set_npc_calc_level_min(form, as_int_or_zero(v)); return; }
    if (eq(field, "CalcLevelMax")) { set_npc_calc_level_max(form, as_int_or_zero(v)); return; }
    if (eq(field, "SpeedMult"))    { set_npc_speed_mult(form, as_int_or_zero(v)); return; }
    if (eq(field, "ChanceNone"))   { set_chance_none(form, as_int_or_zero(v)); return; }

    SKSE::log::debug("[Mora] set: unknown field '{}'", field);
}

void SKSEGameAPI::add(uint32_t target, std::string_view field,
                       const mora::Value& v)
{
    auto* form = RE::TESForm::LookupByID(target);
    if (!form) return;

    if (eq(field, "Keyword")) { add_keyword(form, as_formid_or_zero(v)); return; }
    if (eq(field, "Spell"))   { add_spell  (form, as_formid_or_zero(v)); return; }
    if (eq(field, "Perk"))    { add_perk   (form, as_formid_or_zero(v)); return; }
    if (eq(field, "Faction")) { add_faction(form, as_formid_or_zero(v)); return; }

    SKSE::log::debug("[Mora] add: unknown field '{}'", field);
}

void SKSEGameAPI::remove(uint32_t target, std::string_view field,
                          const mora::Value& v)
{
    auto* form = RE::TESForm::LookupByID(target);
    if (!form) return;

    if (eq(field, "Keyword")) { remove_keyword(form, as_formid_or_zero(v)); return; }

    SKSE::log::debug("[Mora] remove: unknown field '{}'", field);
}

void SKSEGameAPI::multiply(uint32_t target, std::string_view field,
                            const mora::Value& v)
{
    // Multiply = read current × factor → write back. Not exercised by
    // the integration test set. Stubbed with a debug log.
    (void)target; (void)field; (void)v;
    SKSE::log::debug("[Mora] multiply: {} (not implemented in M2)", field);
}

// Factory: plugin_entry constructs this with its own StringPool&
// (populated by apply_snapshot's string interning during the dispatch
// pass). The returned pointer is stored in plugin-global state for the
// session lifetime.
std::unique_ptr<GameAPI> make_skse_game_api(const mora::StringPool& pool) {
    return std::make_unique<SKSEGameAPI>(pool);
}

} // namespace mora_skyrim_runtime

#endif // MORA_WITH_COMMONLIB
