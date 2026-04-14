#ifdef _WIN32
#include "mora/rt/handler_impls.h"

#include <RE/T/TESForm.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/F/FormTraits.h>

#include <cstdint>

namespace mora::rt {

// Cast a TESForm* to its BGSKeywordForm component by trying each concrete
// keyword-bearing form type. Mirrors the pattern in patch_walker.cpp.
static RE::BGSKeywordForm* get_keyword_form(RE::TESForm* form) {
    if (!form) return nullptr;
    if (auto* weap = form->As<RE::TESObjectWEAP>()) return static_cast<RE::BGSKeywordForm*>(weap);
    if (auto* armo = form->As<RE::TESObjectARMO>()) return static_cast<RE::BGSKeywordForm*>(armo);
    if (auto* npc  = form->As<RE::TESNPC>())        return static_cast<RE::BGSKeywordForm*>(npc);
    return nullptr;
}

static EffectHandle effect_ref_add_keyword(const EffectArgs& a) {
    if (a.args.size() < 2) return {};
    uint32_t ref_id = a.args[0];
    uint32_t kw_id  = a.args[1];
    auto* form = RE::TESForm::LookupByID(ref_id);
    auto* kw   = RE::TESForm::LookupByID<RE::BGSKeyword>(kw_id);
    if (!form || !kw) return {};
    auto* kf = get_keyword_form(form);
    if (!kf) return {};
    if (!kf->HasKeyword(kw)) kf->AddKeyword(kw);
    return { (static_cast<uint64_t>(ref_id) << 32) | kw_id };
}

static void retract_ref_add_keyword(EffectHandle h) {
    uint32_t ref_id = static_cast<uint32_t>(h.id >> 32);
    uint32_t kw_id  = static_cast<uint32_t>(h.id);
    auto* form = RE::TESForm::LookupByID(ref_id);
    auto* kw   = RE::TESForm::LookupByID<RE::BGSKeyword>(kw_id);
    if (!form || !kw) return;
    auto* kf = get_keyword_form(form);
    if (!kf) return;
    kf->RemoveKeyword(kw);
}

void bind_ref_handlers(HandlerRegistry& reg) {
    reg.bind_effect(model::HandlerId::RefAddKeyword, effect_ref_add_keyword);
    reg.bind_retract(model::HandlerId::RefAddKeyword, retract_ref_add_keyword);
}

} // namespace mora::rt
#endif
