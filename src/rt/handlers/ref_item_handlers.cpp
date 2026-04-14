#ifdef _WIN32
#include "mora/rt/handler_impls.h"

#include <RE/F/FormTraits.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraDataTypes.h>

#include <cstdint>
#include <unordered_set>

namespace mora::rt {

static RE::TESObjectREFR* lookup_ref(uint32_t fid) {
    auto* f = RE::TESForm::LookupByID(fid);
    return f ? f->As<RE::TESObjectREFR>() : nullptr;
}

// ── is_equipped ─────────────────────────────────────────────────────
// A placed item reference carries ExtraWorn/ExtraWornLeft when it is
// currently equipped on an actor. This is the cleanest cross-check we
// have without walking every actor's inventory.
static std::vector<uint32_t> read_is_equipped(const EffectArgs& a) {
    if (a.args.empty()) return {};
    auto* ref = lookup_ref(a.args[0]);
    if (!ref) return {};
    bool worn = ref->extraList.HasType(RE::ExtraDataType::kWorn)
             || ref->extraList.HasType(RE::ExtraDataType::kWornLeft);
    return worn ? std::vector<uint32_t>{a.args[0]} : std::vector<uint32_t>{};
}

// ── is_stolen ───────────────────────────────────────────────────────
// STUB: CommonLibSSE-NG does not expose a direct stolen flag. Real
// implementation would inspect ExtraOwnership against the reference's
// carrying container's owner, which requires rules we haven't pinned
// down yet. Returns false until the semantics are nailed down.
static std::vector<uint32_t> read_is_stolen(const EffectArgs&) {
    return {};
}

// ── worn_by ─────────────────────────────────────────────────────────
// STUB: reverse lookup from item ref to wearing actor is not exposed
// directly by CommonLibSSE-NG. Implementing this efficiently needs a
// side index maintained from OnItemEquipped / OnItemUnequipped SKSE
// events. Deferred.
static std::vector<uint32_t> read_worn_by(const EffectArgs&) {
    return {};
}

// ── container ──────────────────────────────────────────────────────
// STUB: same story — an item ref in an inventory is not addressable as
// a TESObjectREFR (the inventory stores TESBoundObjects plus counts).
// A real impl would key by (container_id, item_id) through a side index
// of OnItemAdded/OnItemRemoved events.
static std::vector<uint32_t> read_container(const EffectArgs&) {
    return {};
}

void bind_ref_item_handlers(HandlerRegistry& reg,
                            const std::unordered_set<uint16_t>& needed) {
    auto want = [&](model::HandlerId id) {
        return needed.count(static_cast<uint16_t>(id)) > 0;
    };

    if (want(model::HandlerId::RefReadIsEquipped))
        reg.bind_read(model::HandlerId::RefReadIsEquipped, read_is_equipped);
    if (want(model::HandlerId::RefReadIsStolen))
        reg.bind_read(model::HandlerId::RefReadIsStolen, read_is_stolen);
    if (want(model::HandlerId::RefReadWornBy))
        reg.bind_read(model::HandlerId::RefReadWornBy, read_worn_by);
    if (want(model::HandlerId::RefReadContainer))
        reg.bind_read(model::HandlerId::RefReadContainer, read_container);
}

} // namespace mora::rt
#endif
