#ifdef MORA_WITH_COMMONLIB
#include "mora/rt/handler_impls.h"

#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESBoundObject.h>
#include <RE/F/FormTraits.h>

#include <cstdint>
#include <unordered_set>

namespace mora::rt {

static EffectHandle effect_player_add_gold(const EffectArgs& a) {
    if (a.args.size() < 3) return {};
    int32_t amount = static_cast<int32_t>(a.args[2]);
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return {};
    // Gold001 base form (vanilla Skyrim.esm form).
    auto* gold = RE::TESForm::LookupByID(0x0000000Fu);
    if (auto* obj = gold ? gold->As<RE::TESBoundObject>() : nullptr) {
        player->AddObjectToContainer(obj, nullptr, amount, nullptr);
    }
    return { 1 };
}

static EffectHandle effect_player_show_notification(const EffectArgs&) {
    // Notification payload is a string — the args array carries a string
    // offset/id. Real impl looks up the string and calls a notification helper.
    // Stubbed for Plan 3; concrete impl is follow-up work.
    return { 1 };
}

void bind_player_handlers(HandlerRegistry& reg,
                          const std::unordered_set<uint16_t>& needed) {
    auto want = [&](model::HandlerId id) {
        return needed.count(static_cast<uint16_t>(id)) > 0;
    };
    if (want(model::HandlerId::PlayerAddGold))
        reg.bind_effect(model::HandlerId::PlayerAddGold, effect_player_add_gold);
    if (want(model::HandlerId::PlayerShowNotification))
        reg.bind_effect(model::HandlerId::PlayerShowNotification, effect_player_show_notification);
}

} // namespace mora::rt
#endif
