#include "mora/runtime/form_bridge.h"

#ifdef MORA_HAS_COMMONLIB
#include <RE/Skyrim.h>
#endif

namespace mora {

bool FormBridge::apply_patch(uint32_t formid, const FieldPatch& patch) {
#ifdef MORA_HAS_COMMONLIB
    auto* form = RE::TESForm::LookupByID(formid);
    if (!form) return false;

    switch (patch.field) {
    case FieldId::Name: {
        auto* named = form->As<RE::TESFullName>();
        if (!named) return false;
        // TODO: set name from patch.value
        return true;
    }
    case FieldId::GoldValue: {
        auto* valued = form->As<RE::TESValueForm>();
        if (!valued) return false;
        valued->value = static_cast<int32_t>(patch.value.as_int());
        return true;
    }
    case FieldId::Weight: {
        auto* weighted = form->As<RE::TESWeightForm>();
        if (!weighted) return false;
        weighted->weight = static_cast<float>(patch.value.as_float());
        return true;
    }
    case FieldId::Keywords: {
        auto* kwd_form = form->As<RE::BGSKeywordForm>();
        if (!kwd_form) return false;
        auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(patch.value.as_formid());
        if (!keyword) return false;
        if (patch.op == FieldOp::Add) {
            kwd_form->AddKeyword(keyword);
        } else if (patch.op == FieldOp::Remove) {
            kwd_form->RemoveKeyword(keyword);
        }
        return true;
    }
    default:
        return false;
    }
#else
    (void)formid;
    (void)patch;
    return false;
#endif
}

int FormBridge::apply_patches(uint32_t formid, const std::vector<FieldPatch>& patches) {
    int applied = 0;
    for (const auto& patch : patches) {
        if (apply_patch(formid, patch)) {
            ++applied;
        }
    }
    return applied;
}

void FormBridge::populate_facts_for_npc(uint32_t formid, FactDB& db, StringPool& pool) {
#ifdef MORA_HAS_COMMONLIB
    auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(formid);
    if (!npc) return;

    // Keywords
    auto kw_rel = pool.intern("npc_has_keyword");
    if (npc->HasKeywordArray()) {
        for (uint32_t i = 0; i < npc->GetNumKeywords(); ++i) {
            if (auto* kw = npc->GetKeywordAt(i)) {
                db.add_fact(kw_rel, {Value::make_formid(formid), Value::make_formid(kw->GetFormID())});
            }
        }
    }

    // Factions
    auto fac_rel = pool.intern("npc_in_faction");
    for (const auto& fac_data : npc->factions) {
        if (fac_data.faction) {
            db.add_fact(fac_rel, {Value::make_formid(formid), Value::make_formid(fac_data.faction->GetFormID())});
        }
    }

    // Race
    if (npc->race) {
        auto race_rel = pool.intern("npc_race");
        db.add_fact(race_rel, {Value::make_formid(formid), Value::make_formid(npc->race->GetFormID())});
    }
#else
    (void)formid;
    (void)db;
    (void)pool;
#endif
}

} // namespace mora
