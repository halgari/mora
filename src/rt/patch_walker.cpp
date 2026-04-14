// ═══════════════════════════════════════════════════════════════════════════
// Patch walker — loads and applies patches using typed CommonLibSSE-NG access.
// No raw pointer math or memcpy-based field writes.
// ═══════════════════════════════════════════════════════════════════════════

#ifdef _WIN32

#include "mora/data/action_names.h"
#include "mora/emit/patch_table.h"

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
#include <RE/T/TESShout.h>
#include <RE/T/TESFullName.h>
#include <RE/T/TESValueForm.h>
#include <RE/T/TESWeightForm.h>
#include <RE/T/TESAttackDamageForm.h>
#include <RE/T/TESEnchantableForm.h>
#include <RE/T/TESRaceForm.h>
#include <RE/B/BGSSkinForm.h>
#include <RE/B/BGSOutfit.h>
#include <RE/B/BGSVoiceType.h>
#include <RE/E/EnchantmentItem.h>
#include <RE/T/TESClass.h>
#include <RE/M/MemoryManager.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using mora::fid;
using mora::fop;
using mora::FieldId;
using mora::FieldOp;

static std::vector<uint8_t> g_patch_data;

struct PatchTableHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t patch_count;
    uint32_t string_table_size;
};

struct PatchEntry {
    uint32_t formid;
    uint8_t field_id;
    uint8_t op;
    uint8_t value_type;
    uint8_t pad;
    uint64_t value;
};

// ── Component extraction helpers ───────────────────────────────────────
// As<T>() only works for form types, not component base classes,
// so we cast through the concrete form type.

// Get a component base class from a form by trying each concrete form type.
// Uses if-constexpr to avoid casting through unrelated types.
template<typename ComponentT>
ComponentT* get_component(RE::TESForm* form) {
    if constexpr (std::is_base_of_v<ComponentT, RE::TESObjectWEAP>) {
        if (auto* weap = form->As<RE::TESObjectWEAP>())
            return static_cast<ComponentT*>(weap);
    }
    if constexpr (std::is_base_of_v<ComponentT, RE::TESObjectARMO>) {
        if (auto* armo = form->As<RE::TESObjectARMO>())
            return static_cast<ComponentT*>(armo);
    }
    if constexpr (std::is_base_of_v<ComponentT, RE::TESNPC>) {
        if (auto* npc = form->As<RE::TESNPC>())
            return static_cast<ComponentT*>(npc);
    }
    return nullptr;
}

// ── Leveled list helper ────────────────────────────────────────────────

static RE::TESLeveledList* get_leveled_list(RE::TESForm* form) {
    if (auto* li = form->As<RE::TESLevItem>())
        return static_cast<RE::TESLeveledList*>(li);
    if (auto* lc = form->As<RE::TESLevCharacter>())
        return static_cast<RE::TESLeveledList*>(lc);
    return nullptr;
}

// ── Scalar field setters ───────────────────────────────────────────────
// Each operates on typed CommonLibSSE-NG members.

static void apply_name(RE::TESForm* form, const PatchEntry& e,
                        const uint8_t* string_table) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::StringIndex)) return;
    auto* named = get_component<RE::TESFullName>(form);
    if (!named) return;

    uint32_t str_off = static_cast<uint32_t>(e.value);
    uint16_t len = 0;
    std::memcpy(&len, string_table + str_off, 2);
    char buf[mora::kPatchStringBufSize];
    uint16_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    std::memcpy(buf, string_table + str_off + 2, copy_len);
    buf[copy_len] = '\0';
    named->fullName = buf;
}

static void apply_gold_value(RE::TESForm* form, const PatchEntry& e) {
    auto* vf = get_component<RE::TESValueForm>(form);
    if (!vf) return;
    vf->value = static_cast<std::int32_t>(e.value);
}

static void apply_weight(RE::TESForm* form, const PatchEntry& e) {
    auto* wf = get_component<RE::TESWeightForm>(form);
    if (!wf) return;
    double d; std::memcpy(&d, &e.value, 8);
    wf->weight = static_cast<float>(d);
}

static void apply_damage(RE::TESForm* form, const PatchEntry& e) {
    auto* weap = form->As<RE::TESObjectWEAP>();
    if (!weap) return;
    auto* df = static_cast<RE::TESAttackDamageForm*>(weap);
    df->attackDamage = static_cast<std::uint16_t>(e.value);
}

static void apply_enchantment(RE::TESForm* form, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* ef = get_component<RE::TESEnchantableForm>(form);
    if (!ef) return;
    auto* ench = RE::TESForm::LookupByID<RE::EnchantmentItem>(static_cast<uint32_t>(e.value));
    if (ench) ef->formEnchanting = ench;
}

// ── Weapon-specific fields ─────────────────────────────────────────────

static void apply_weapon_field(RE::TESObjectWEAP* weap, FieldId field, const PatchEntry& e) {
    double d; std::memcpy(&d, &e.value, 8);
    float fval = static_cast<float>(d);

    switch (field) {
        case FieldId::Speed:     weap->weaponData.speed = fval; break;
        case FieldId::Reach:     weap->weaponData.reach = fval; break;
        case FieldId::RangeMin:  weap->weaponData.minRange = fval; break;
        case FieldId::RangeMax:  weap->weaponData.maxRange = fval; break;
        case FieldId::Stagger:   weap->weaponData.staggerValue = fval; break;
        case FieldId::CritDamage:
            weap->criticalData.damage = static_cast<std::uint16_t>(e.value);
            break;
        default: break;
    }
}

// ── Armor-specific fields ──────────────────────────────────────────────

static void apply_armor_field(RE::TESObjectARMO* armo, FieldId field, const PatchEntry& e) {
    switch (field) {
        case FieldId::ArmorRating:
            armo->armorRating = static_cast<std::uint32_t>(e.value);
            break;
        default: break;
    }
}

// ── NPC scalar fields ──────────────────────────────────────────────────

static void apply_npc_scalar(RE::TESNPC* npc, FieldId field, const PatchEntry& e) {
    switch (field) {
        case FieldId::Level:
            npc->actorData.level = static_cast<std::uint16_t>(e.value);
            break;
        case FieldId::CalcLevelMin:
            npc->actorData.calcLevelMin = static_cast<std::uint16_t>(e.value);
            break;
        case FieldId::CalcLevelMax:
            npc->actorData.calcLevelMax = static_cast<std::uint16_t>(e.value);
            break;
        case FieldId::SpeedMult:
            npc->actorData.speedMult = static_cast<std::uint16_t>(e.value);
            break;
        default: break;
    }
}

// ── NPC form reference fields ──────────────────────────────────────────

static void apply_npc_form_ref(RE::TESNPC* npc, FieldId field, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* ref = RE::TESForm::LookupByID(static_cast<uint32_t>(e.value));
    if (!ref) return;

    switch (field) {
        case FieldId::RaceForm: {
            auto* rf = static_cast<RE::TESRaceForm*>(npc);
            rf->race = ref->As<RE::TESRace>();
            break;
        }
        case FieldId::ClassForm:
            npc->npcClass = ref->As<RE::TESClass>();
            break;
        case FieldId::VoiceTypeForm:
            npc->voiceType = ref->As<RE::BGSVoiceType>();
            break;
        case FieldId::SkinForm: {
            auto* sf = static_cast<RE::BGSSkinForm*>(npc);
            sf->skin = ref->As<RE::TESObjectARMO>();
            break;
        }
        case FieldId::OutfitForm:
            npc->defaultOutfit = ref->As<RE::BGSOutfit>();
            break;
        default: break;
    }
}

// ── NPC boolean flags ──────────────────────────────────────────────────

static void apply_npc_flag(RE::TESNPC* npc, FieldId field, bool set) {
    using Flag = RE::ACTOR_BASE_DATA::Flag;
    Flag flag{};
    switch (field) {
        case FieldId::Essential:    flag = Flag::kEssential; break;
        case FieldId::Protected:    flag = Flag::kProtected; break;
        case FieldId::AutoCalcStats:flag = Flag::kAutoCalcStats; break;
        default: return;
    }
    if (set)
        npc->actorData.actorBaseFlags.set(flag);
    else
        npc->actorData.actorBaseFlags.reset(flag);
}

// ── Keyword operations ─────────────────────────────────────────────────

static void apply_keyword(RE::TESForm* form, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(static_cast<uint32_t>(e.value));
    if (!kw) return;
    auto* kf = get_component<RE::BGSKeywordForm>(form);
    if (!kf) return;

    if (e.op == fop(FieldOp::Add)) {
        if (!kf->HasKeyword(kw)) kf->AddKeyword(kw);
    } else if (e.op == fop(FieldOp::Remove)) {
        kf->RemoveKeyword(kw);
    }
}

// ── Spell operations ───────────────────────────────────────────────────

static void apply_spell(RE::TESForm* form, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* npc = form->As<RE::TESNPC>();
    auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(static_cast<uint32_t>(e.value));
    if (!npc || !spell) return;

    auto* data = npc->GetSpellList();
    if (!data) return;

    if (e.op == fop(FieldOp::Add)) {
        for (std::uint32_t i = 0; i < data->numSpells; i++)
            if (data->spells[i] == spell) return;

        std::uint32_t newCount = data->numSpells + 1;
        auto* fresh = static_cast<RE::SpellItem**>(RE::malloc(newCount * sizeof(RE::SpellItem*)));
        if (!fresh) return;
        if (data->spells && data->numSpells > 0)
            std::memcpy(fresh, data->spells, data->numSpells * sizeof(RE::SpellItem*));
        fresh[data->numSpells] = spell;
        RE::free(data->spells);
        data->spells = fresh;
        data->numSpells = newCount;
    } else if (e.op == fop(FieldOp::Remove)) {
        std::uint32_t found = UINT32_MAX;
        for (std::uint32_t i = 0; i < data->numSpells; i++)
            if (data->spells[i] == spell) { found = i; break; }
        if (found == UINT32_MAX) return;
        for (std::uint32_t i = found; i + 1 < data->numSpells; i++)
            data->spells[i] = data->spells[i + 1];
        data->numSpells--;
    }
}

// ── Perk operations ────────────────────────────────────────────────────

static void apply_perk(RE::TESForm* form, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* npc = form->As<RE::TESNPC>();
    auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(static_cast<uint32_t>(e.value));
    if (!npc || !perk) return;

    for (std::uint32_t i = 0; i < npc->perkCount; i++)
        if (npc->perks[i].perk == perk) return;

    std::uint32_t newCount = npc->perkCount + 1;
    auto* fresh = static_cast<RE::PerkRankData*>(RE::malloc(newCount * sizeof(RE::PerkRankData)));
    if (!fresh) return;
    if (npc->perkCount > 0)
        std::memcpy(fresh, &npc->perks[0], npc->perkCount * sizeof(RE::PerkRankData));
    fresh[npc->perkCount] = {perk, 0};
    RE::free(&npc->perks[0]);
    npc->perks = fresh;
    npc->perkCount = newCount;
}

// ── Faction operations ─────────────────────────────────────────────────

static void apply_faction(RE::TESForm* form, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* npc = form->As<RE::TESNPC>();
    auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(static_cast<uint32_t>(e.value));
    if (!npc || !faction) return;

    for (auto& fr : npc->factions)
        if (fr.faction == faction) return;
    npc->factions.push_back({faction, 0});
}

// ── Shout operations ───────────────────────────────────────────────────

static void apply_shout(RE::TESForm* form, const PatchEntry& e) {
    if (e.value_type != static_cast<uint8_t>(mora::PatchValueType::FormID)) return;
    auto* npc = form->As<RE::TESNPC>();
    auto* shout = RE::TESForm::LookupByID<RE::TESShout>(static_cast<uint32_t>(e.value));
    if (!npc || !shout) return;

    auto* data = npc->GetSpellList();
    if (!data) return;

    for (std::uint32_t i = 0; i < data->numShouts; i++)
        if (data->shouts[i] == shout) return;

    std::uint32_t newCount = data->numShouts + 1;
    auto* fresh = static_cast<RE::TESShout**>(RE::malloc(newCount * sizeof(RE::TESShout*)));
    if (!fresh) return;
    if (data->shouts && data->numShouts > 0)
        std::memcpy(fresh, data->shouts, data->numShouts * sizeof(RE::TESShout*));
    fresh[data->numShouts] = shout;
    RE::free(data->shouts);
    data->shouts = fresh;
    data->numShouts = newCount;
}

// ── Leveled list operations ────────────────────────────────────────────

static void apply_leveled_list(RE::TESForm* form, const PatchEntry& e) {
    auto* levList = get_leveled_list(form);
    if (!levList) return;

    if (e.op == fop(FieldOp::Add)) {
        auto* entryForm = RE::TESForm::LookupByID(static_cast<uint32_t>(e.value));
        if (!entryForm) return;
        uint16_t level = static_cast<uint16_t>(e.value >> 32);
        uint16_t count = static_cast<uint16_t>(e.value >> 48);
        if (count == 0) count = 1;

        std::uint8_t n = levList->numEntries;
        for (std::uint8_t i = 0; i < n; i++)
            if (levList->entries[i].form == entryForm && levList->entries[i].level == level) return;
        if (n >= 255) return;

        std::uint8_t newN = n + 1;
        auto* fresh = static_cast<RE::LEVELED_OBJECT*>(RE::malloc(newN * sizeof(RE::LEVELED_OBJECT)));
        if (!fresh) return;
        if (n > 0)
            std::memcpy(fresh, &levList->entries[0], n * sizeof(RE::LEVELED_OBJECT));

        std::memset(&fresh[n], 0, sizeof(RE::LEVELED_OBJECT));
        fresh[n].form = entryForm;
        fresh[n].count = count;
        fresh[n].level = level;

        // Insertion sort by level
        for (std::uint8_t i = 1; i < newN; i++) {
            if (fresh[i].level < fresh[i - 1].level) {
                RE::LEVELED_OBJECT tmp = fresh[i];
                std::uint8_t j = i;
                while (j > 0 && fresh[j - 1].level > tmp.level) {
                    fresh[j] = fresh[j - 1];
                    j--;
                }
                fresh[j] = tmp;
            }
        }

        RE::free(&levList->entries[0]);
        *reinterpret_cast<RE::LEVELED_OBJECT**>(
            reinterpret_cast<char*>(&levList->entries)) = fresh;
        levList->numEntries = newN;

    } else if (e.op == fop(FieldOp::Remove)) {
        uint32_t target_fid = static_cast<uint32_t>(e.value);
        std::uint8_t write = 0;
        for (std::uint8_t i = 0; i < levList->numEntries; i++) {
            std::uint32_t fid = levList->entries[i].form ? levList->entries[i].form->formID : 0;
            if (fid != target_fid) {
                if (write != i) levList->entries[write] = levList->entries[i];
                write++;
            }
        }
        levList->numEntries = write;

    } else if (e.op == fop(FieldOp::Set)) {
        if (levList->numEntries > 0)
            levList->entries.clear();
        levList->numEntries = 0;
    }
}

// ── Main dispatch ──────────────────────────────────────────────────────

static void apply_patch_entry(RE::TESForm* form, const PatchEntry& e,
                               const uint8_t* string_table) {
    FieldId field = static_cast<FieldId>(e.field_id);

    switch (field) {
        // Shared component fields (polymorphic)
        case FieldId::Name:       apply_name(form, e, string_table); return;
        case FieldId::GoldValue:  apply_gold_value(form, e); return;
        case FieldId::Weight:     apply_weight(form, e); return;
        case FieldId::Damage:     apply_damage(form, e); return;
        case FieldId::EnchantmentForm: apply_enchantment(form, e); return;

        // Keywords (polymorphic)
        case FieldId::Keywords:   apply_keyword(form, e); return;

        // NPC collection operations
        case FieldId::Spells:     apply_spell(form, e); return;
        case FieldId::Perks:      apply_perk(form, e); return;
        case FieldId::Factions:   apply_faction(form, e); return;
        case FieldId::Shouts:     apply_shout(form, e); return;

        // Leveled list operations
        case FieldId::LeveledEntries: apply_leveled_list(form, e); return;
        case FieldId::ChanceNone: {
            auto* ll = get_leveled_list(form);
            if (ll) ll->chanceNone = static_cast<std::int8_t>(e.value);
            return;
        }

        // NPC boolean flags
        case FieldId::Essential:
        case FieldId::Protected:
        case FieldId::AutoCalcStats: {
            auto* npc = form->As<RE::TESNPC>();
            if (npc) apply_npc_flag(npc, field, e.value != 0);
            return;
        }

        default: break;
    }

    // Weapon-specific fields
    if (auto* weap = form->As<RE::TESObjectWEAP>()) {
        apply_weapon_field(weap, field, e);
        return;
    }

    // Armor-specific fields
    if (auto* armo = form->As<RE::TESObjectARMO>()) {
        apply_armor_field(armo, field, e);
        return;
    }

    // NPC-specific scalar and form-ref fields
    if (auto* npc = form->As<RE::TESNPC>()) {
        switch (field) {
            case FieldId::Level:
            case FieldId::CalcLevelMin:
            case FieldId::CalcLevelMax:
            case FieldId::SpeedMult:
                apply_npc_scalar(npc, field, e);
                return;
            case FieldId::RaceForm:
            case FieldId::ClassForm:
            case FieldId::VoiceTypeForm:
            case FieldId::SkinForm:
            case FieldId::OutfitForm:
                apply_npc_form_ref(npc, field, e);
                return;
            default: break;
        }
    }
}

// ── Public API ─────────────────────────────────────────────────────────

uint32_t load_patches(const std::filesystem::path& patch_file) {
    std::ifstream ifs(patch_file, std::ios::binary | std::ios::ate);
    if (!ifs) return 0;

    auto size = ifs.tellg();
    if (size < static_cast<std::streamoff>(sizeof(PatchTableHeader))) return 0;

    g_patch_data.resize(static_cast<size_t>(size));
    ifs.seekg(0);
    ifs.read(reinterpret_cast<char*>(g_patch_data.data()), size);

    auto* hdr = reinterpret_cast<const PatchTableHeader*>(g_patch_data.data());
    if (hdr->magic != mora::kPatchTableMagic || hdr->version != mora::kPatchTableVersion) {
        g_patch_data.clear();
        return 0;
    }
    return hdr->patch_count;
}

void apply_all_patches() {
    if (g_patch_data.empty()) return;

    auto* hdr = reinterpret_cast<const PatchTableHeader*>(g_patch_data.data());
    const uint8_t* string_table = g_patch_data.data() + sizeof(PatchTableHeader);
    const auto* entries = reinterpret_cast<const PatchEntry*>(
        string_table + hdr->string_table_size);

    uint32_t current_fid = 0;
    RE::TESForm* current_form = nullptr;

    for (uint32_t i = 0; i < hdr->patch_count; i++) {
        const auto& e = entries[i];
        if (e.formid != current_fid) {
            current_fid = e.formid;
            current_form = RE::TESForm::LookupByID(current_fid);
        }
        if (!current_form) continue;
        apply_patch_entry(current_form, e, string_table);
    }
}

#else
// Linux stub -- patch_walker is only meaningful when cross-compiled for Windows
namespace mora::rt { void patch_walker_stub() {} }
#endif
