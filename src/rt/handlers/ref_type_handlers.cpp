#ifdef _WIN32
#include "mora/rt/handler_impls.h"

#include <RE/F/FormTraits.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESBoundObject.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/F/FormTypes.h>

#include <cstdint>
#include <unordered_set>

namespace mora::rt {

// Predicates return an empty vector when false. When true they return a
// 1-element vector containing the subject FormID (arbitrary sentinel —
// the engine only looks at emptiness for predicate facts, but we pass
// the ref through to make the result traceable).

static RE::TESForm* lookup_form(uint32_t fid) {
    return RE::TESForm::LookupByID(fid);
}

// Resolve a ref's base form type. If the form is itself a base (e.g. the
// caller passed a WEAP FormID) we fall back to its own type.
static RE::FormType base_form_type(uint32_t fid) {
    auto* f = lookup_form(fid);
    if (!f) return RE::FormType::None;
    if (auto* ref = f->As<RE::TESObjectREFR>()) {
        auto* base = ref->GetBaseObject();
        return base ? base->GetFormType() : RE::FormType::None;
    }
    return f->GetFormType();
}

static std::vector<uint32_t> read_is_player(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return {};
    return (player->GetFormID() == a.args[0])
        ? std::vector<uint32_t>{a.args[0]}
        : std::vector<uint32_t>{};
}

static std::vector<uint32_t> read_is_npc(const EffectArgs& a) {
    if (a.args.empty()) return {};
    return base_form_type(a.args[0]) == RE::FormType::NPC
        ? std::vector<uint32_t>{a.args[0]}
        : std::vector<uint32_t>{};
}

static std::vector<uint32_t> read_is_weapon(const EffectArgs& a) {
    if (a.args.empty()) return {};
    return base_form_type(a.args[0]) == RE::FormType::Weapon
        ? std::vector<uint32_t>{a.args[0]}
        : std::vector<uint32_t>{};
}

static std::vector<uint32_t> read_is_armor(const EffectArgs& a) {
    if (a.args.empty()) return {};
    return base_form_type(a.args[0]) == RE::FormType::Armor
        ? std::vector<uint32_t>{a.args[0]}
        : std::vector<uint32_t>{};
}

void bind_ref_type_handlers(HandlerRegistry& reg,
                            const std::unordered_set<uint16_t>& needed) {
    auto want = [&](model::HandlerId id) {
        return needed.count(static_cast<uint16_t>(id)) > 0;
    };
    if (want(model::HandlerId::RefReadIsPlayer))
        reg.bind_read(model::HandlerId::RefReadIsPlayer, read_is_player);
    if (want(model::HandlerId::RefReadIsNpc))
        reg.bind_read(model::HandlerId::RefReadIsNpc, read_is_npc);
    if (want(model::HandlerId::RefReadIsWeapon))
        reg.bind_read(model::HandlerId::RefReadIsWeapon, read_is_weapon);
    if (want(model::HandlerId::RefReadIsArmor))
        reg.bind_read(model::HandlerId::RefReadIsArmor, read_is_armor);
}

} // namespace mora::rt
#endif
