#ifdef MORA_WITH_COMMONLIB
#include "mora/rt/handler_impls.h"

#include <RE/F/FormTraits.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESBoundObject.h>
#include <RE/A/Actor.h>
#include <RE/A/ActorValues.h>
#include <RE/B/BGSLocation.h>
#include <RE/B/BGSBipedObjectForm.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/M/MagicItem.h>
#include <RE/I/InventoryEntryData.h>

#include <cstdint>
#include <cstring>
#include <unordered_set>

namespace mora::rt {

// Encode an IEEE-754 float into a uint32_t for transport through the
// engine's value slots. Consumers that know the column is Float decode
// with the reverse memcpy.
static inline uint32_t float_bits(float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    return bits;
}

static RE::Actor* lookup_actor(uint32_t fid) {
    auto* f = RE::TESForm::LookupByID(fid);
    if (!f) return nullptr;
    // Direct actor lookup, then fall back to a ref whose base is an actor.
    if (auto* a = f->As<RE::Actor>()) return a;
    if (auto* ref = f->As<RE::TESObjectREFR>()) {
        return ref->As<RE::Actor>();
    }
    return nullptr;
}

static RE::TESObjectREFR* lookup_ref(uint32_t fid) {
    auto* f = RE::TESForm::LookupByID(fid);
    return f ? f->As<RE::TESObjectREFR>() : nullptr;
}

// ── Predicates ────────────────────────────────────────────────────────

static std::vector<uint32_t> read_is_dead(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    return actor->IsDead() ? std::vector<uint32_t>{a.args[0]} : std::vector<uint32_t>{};
}

static std::vector<uint32_t> read_in_combat(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    return actor->IsInCombat() ? std::vector<uint32_t>{a.args[0]} : std::vector<uint32_t>{};
}

// ── const<Float>: [ref_id, float_bits] ────────────────────────────────

static std::vector<uint32_t> read_actor_value(const EffectArgs& a, RE::ActorValue av) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    float v = actor->AsActorValueOwner()->GetActorValue(av);
    return {a.args[0], float_bits(v)};
}

static std::vector<uint32_t> read_health(const EffectArgs& a)  { return read_actor_value(a, RE::ActorValue::kHealth); }
static std::vector<uint32_t> read_magicka(const EffectArgs& a) { return read_actor_value(a, RE::ActorValue::kMagicka); }
static std::vector<uint32_t> read_stamina(const EffectArgs& a) { return read_actor_value(a, RE::ActorValue::kStamina); }

// ── const<Int>: [ref_id, int_value] ───────────────────────────────────

static std::vector<uint32_t> read_level(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    return {a.args[0], static_cast<uint32_t>(actor->GetLevel())};
}

// ── const<FormRef>: [ref_id, form_id] ─────────────────────────────────

static std::vector<uint32_t> read_base_form(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* ref = lookup_ref(a.args[0]);
    if (!ref) return {};
    auto* base = ref->GetBaseObject();
    if (!base) return {};
    return {a.args[0], base->GetFormID()};
}

static std::vector<uint32_t> read_current_location(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* ref = lookup_ref(a.args[0]);
    if (!ref) return {};
    auto* loc = ref->GetCurrentLocation();
    if (!loc) return {};
    return {a.args[0], loc->GetFormID()};
}

static std::vector<uint32_t> read_equipped_hand(const EffectArgs& a, bool left) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    auto* obj = actor->GetEquippedObject(left);
    if (!obj) return {};
    return {a.args[0], obj->GetFormID()};
}

static std::vector<uint32_t> read_equipped_weapon(const EffectArgs& a)       { return read_equipped_hand(a, false); }
static std::vector<uint32_t> read_equipped_weapon_left(const EffectArgs& a)  { return read_equipped_hand(a, true); }

// Spells are read from selectedSpells[SlotTypes::kLeftHand / kRightHand].
static std::vector<uint32_t> read_equipped_spell(const EffectArgs& a, int slot) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    auto* spell = actor->GetActorRuntimeData().selectedSpells[slot];
    if (!spell) return {};
    return {a.args[0], spell->GetFormID()};
}

static std::vector<uint32_t> read_equipped_spell_left(const EffectArgs& a) {
    return read_equipped_spell(a, RE::Actor::SlotTypes::kLeftHand);
}
static std::vector<uint32_t> read_equipped_spell_right(const EffectArgs& a) {
    return read_equipped_spell(a, RE::Actor::SlotTypes::kRightHand);
}

// ── list<FormRef>: [actor_id, item_id_0, actor_id, item_id_1, ...] ────
// The engine consumes per-tuple results in pairs (key, value) ordered
// by declared args. For a list we flatten into [key, v0, key, v1, ...].

static std::vector<uint32_t> read_equipped_armor(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* actor = lookup_actor(a.args[0]);
    if (!actor) return {};
    std::vector<uint32_t> out;
    // Iterate every biped slot bit and dedupe by FormID.
    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
    constexpr Slot kSlots[] = {
        Slot::kHead, Slot::kHair, Slot::kBody, Slot::kHands,
        Slot::kForearms, Slot::kAmulet, Slot::kRing, Slot::kFeet,
        Slot::kCalves, Slot::kShield, Slot::kTail, Slot::kLongHair,
        Slot::kCirclet, Slot::kEars
    };
    std::vector<uint32_t> seen;
    for (auto s : kSlots) {
        auto* armo = actor->GetWornArmor(s);
        if (!armo) continue;
        uint32_t fid = armo->GetFormID();
        bool dup = false;
        for (auto x : seen) if (x == fid) { dup = true; break; }
        if (dup) continue;
        seen.push_back(fid);
        out.push_back(a.args[0]);
        out.push_back(fid);
    }
    return out;
}

static std::vector<uint32_t> read_inventory_item(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* ref = lookup_ref(a.args[0]);
    if (!ref) return {};
    std::vector<uint32_t> out;
    auto inv = ref->GetInventory();
    for (auto& [obj, pair] : inv) {
        if (!obj) continue;
        if (pair.first <= 0) continue;
        out.push_back(a.args[0]);
        out.push_back(obj->GetFormID());
    }
    return out;
}

void bind_ref_actor_handlers(HandlerRegistry& reg,
                             const std::unordered_set<uint16_t>& needed) {
    auto want = [&](model::HandlerId id) {
        return needed.count(static_cast<uint16_t>(id)) > 0;
    };

    if (want(model::HandlerId::RefReadIsDead))
        reg.bind_read(model::HandlerId::RefReadIsDead, read_is_dead);
    if (want(model::HandlerId::RefReadInCombat))
        reg.bind_read(model::HandlerId::RefReadInCombat, read_in_combat);

    if (want(model::HandlerId::RefReadHealth))
        reg.bind_read(model::HandlerId::RefReadHealth, read_health);
    if (want(model::HandlerId::RefReadMagicka))
        reg.bind_read(model::HandlerId::RefReadMagicka, read_magicka);
    if (want(model::HandlerId::RefReadStamina))
        reg.bind_read(model::HandlerId::RefReadStamina, read_stamina);
    if (want(model::HandlerId::RefReadLevel))
        reg.bind_read(model::HandlerId::RefReadLevel, read_level);

    if (want(model::HandlerId::RefReadBaseForm))
        reg.bind_read(model::HandlerId::RefReadBaseForm, read_base_form);
    if (want(model::HandlerId::RefReadCurrentLocation))
        reg.bind_read(model::HandlerId::RefReadCurrentLocation, read_current_location);

    if (want(model::HandlerId::RefReadEquippedWeapon))
        reg.bind_read(model::HandlerId::RefReadEquippedWeapon, read_equipped_weapon);
    if (want(model::HandlerId::RefReadEquippedWeaponLeft))
        reg.bind_read(model::HandlerId::RefReadEquippedWeaponLeft, read_equipped_weapon_left);
    if (want(model::HandlerId::RefReadEquippedSpellLeft))
        reg.bind_read(model::HandlerId::RefReadEquippedSpellLeft, read_equipped_spell_left);
    if (want(model::HandlerId::RefReadEquippedSpellRight))
        reg.bind_read(model::HandlerId::RefReadEquippedSpellRight, read_equipped_spell_right);
    if (want(model::HandlerId::RefReadEquippedArmor))
        reg.bind_read(model::HandlerId::RefReadEquippedArmor, read_equipped_armor);
    if (want(model::HandlerId::RefReadInventoryItem))
        reg.bind_read(model::HandlerId::RefReadInventoryItem, read_inventory_item);
}

} // namespace mora::rt
#endif
