#include "mora/runtime/form_bridge.h"

#ifdef _WIN32
#include "mora/runtime/skyrim_abi.h"
#endif

#ifdef _WIN32
// Global function pointer — resolved at plugin load via Address Library
skyrim::LookupFormByID_t skyrim::g_lookupFormByID = nullptr;
#endif

namespace mora {

#ifdef _WIN32

static skyrim::TESForm* lookup_form(uint32_t formid) {
    if (skyrim::g_lookupFormByID)
        return skyrim::g_lookupFormByID(formid);
    return nullptr;
}

bool FormBridge::apply_patch(uint32_t formid, const FieldPatch& patch) {
    auto* form = lookup_form(formid);
    if (!form) return false;

    switch (patch.field) {
        case FieldId::Keywords: {
            auto* kf = skyrim::get_keyword_form(form);
            if (!kf) return false;
            // For keyword add/remove, we need to reallocate the keyword array.
            // This requires Skyrim's heap allocator. For Phase 1, log and skip.
            // TODO: implement keyword array mutation via Skyrim heap
            return false;
        }

        case FieldId::Damage: {
            auto* dmg = skyrim::get_attack_damage_form(form);
            if (!dmg) return false;
            dmg->attackDamage = static_cast<uint16_t>(patch.value.as_int());
            return true;
        }

        case FieldId::ArmorRating: {
            // Direct memory write at known offset
            if (static_cast<skyrim::FormType>(form->formType) == skyrim::FormType::Armor) {
                auto* rating = reinterpret_cast<uint32_t*>(
                    reinterpret_cast<char*>(form) + skyrim::armor_offsets::armor_rating);
                *rating = static_cast<uint32_t>(patch.value.as_int());
                return true;
            }
            return false;
        }

        case FieldId::GoldValue: {
            auto* vf = skyrim::get_value_form(form);
            if (!vf) return false;
            vf->value = static_cast<int32_t>(patch.value.as_int());
            return true;
        }

        case FieldId::Weight: {
            auto* wf = skyrim::get_weight_form(form);
            if (!wf) return false;
            wf->weight = static_cast<float>(patch.value.as_float());
            return true;
        }

        case FieldId::Name: {
            // SetFullName requires calling a Skyrim function (not just a memory write)
            // because BSFixedString is ref-counted and interned.
            // TODO: resolve SetFullName via Address Library
            return false;
        }

        case FieldId::Spells:
        case FieldId::Perks:
        case FieldId::Factions:
        case FieldId::Items: {
            // These require array reallocation via Skyrim's heap.
            // TODO: implement via Address Library function resolution
            return false;
        }

        default:
            return false;
    }
}

#else // !_WIN32

bool FormBridge::apply_patch(uint32_t formid, const FieldPatch& patch) {
    (void)formid; (void)patch;
    return false;
}

#endif // _WIN32

int FormBridge::apply_patches(uint32_t formid, const std::vector<FieldPatch>& patches) {
    int applied = 0;
    for (const auto& p : patches) {
        if (apply_patch(formid, p)) applied++;
    }
    return applied;
}

void FormBridge::populate_facts_for_npc(uint32_t formid, FactDB& db, StringPool& pool) {
#ifdef _WIN32
    auto* form = lookup_form(formid);
    if (!form) return;
    if (static_cast<skyrim::FormType>(form->formType) != skyrim::FormType::NPC) return;

    // NPC existence
    db.add_fact(pool.intern("npc"), {Value::make_formid(formid)});

    // Keywords
    auto* kf = skyrim::get_keyword_form(form);
    if (kf && kf->keywords) {
        auto has_kw = pool.intern("has_keyword");
        for (uint32_t i = 0; i < kf->numKeywords; i++) {
            if (kf->keywords[i]) {
                auto* kw = reinterpret_cast<skyrim::TESForm*>(kf->keywords[i]);
                db.add_fact(has_kw, {Value::make_formid(formid),
                                      Value::make_formid(kw->formID)});
            }
        }
    }

    // Factions (BSTArray<FACTION_RANK> at known offset)
    auto* factions = skyrim::component_at<skyrim::BSTArray<skyrim::FACTION_RANK>>(
        form, skyrim::npc_offsets::factions);
    if (factions && factions->data) {
        auto has_faction = pool.intern("has_faction");
        for (uint32_t i = 0; i < factions->size; i++) {
            if (factions->data[i].faction) {
                auto* fac = reinterpret_cast<skyrim::TESForm*>(factions->data[i].faction);
                db.add_fact(has_faction, {Value::make_formid(formid),
                                           Value::make_formid(fac->formID)});
            }
        }
    }

    // Race
    // TESRaceForm at npc_offsets::race_form has a TESRace* at offset 0x08
    auto* race_ptr = *reinterpret_cast<skyrim::TESRace**>(
        reinterpret_cast<char*>(form) + skyrim::npc_offsets::race_form + 0x08);
    if (race_ptr) {
        auto race_of = pool.intern("race_of");
        auto* race_form = reinterpret_cast<skyrim::TESForm*>(race_ptr);
        db.add_fact(race_of, {Value::make_formid(formid),
                               Value::make_formid(race_form->formID)});
    }
#else
    (void)formid; (void)db; (void)pool;
#endif
}

} // namespace mora
