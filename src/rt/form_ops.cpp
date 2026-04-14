// Platform-neutral code (used by IR emitter on Linux too)
#include "mora/rt/form_ops.h"
#include "mora/data/form_model.h"
#include "mora/data/action_names.h"

using namespace mora;

namespace mora::rt {

uint64_t get_field_offset(uint8_t ft, uint16_t field_id) {
    return model::field_offset_for(ft, static_cast<FieldId>(field_id));
}

} // namespace mora::rt

// ── Windows-only: CommonLibSSE-NG runtime operations ────────────────
#ifdef _WIN32

#include <RE/T/TESForm.h>
#include <RE/T/TESNPC.h>
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
#include <RE/M/MemoryManager.h>

#include <cstring>

// ── Helpers ─────────────────────────────────────────────────────────

namespace {

// Get BGSKeywordForm component from a form (NPC, Weapon, or Armor).
// As<T>() only works for form types, not component base classes,
// so we cast through the concrete type.
RE::BGSKeywordForm* get_keyword_form(RE::TESForm* form) {
    if (auto* npc = form->As<RE::TESNPC>())
        return static_cast<RE::BGSKeywordForm*>(npc);
    if (auto* weap = form->As<RE::TESObjectWEAP>())
        return static_cast<RE::BGSKeywordForm*>(weap);
    if (auto* armo = form->As<RE::TESObjectARMO>())
        return static_cast<RE::BGSKeywordForm*>(armo);
    return nullptr;
}

// Get TESFullName component from a form.
RE::TESFullName* get_full_name(RE::TESForm* form) {
    if (auto* npc = form->As<RE::TESNPC>())
        return static_cast<RE::TESFullName*>(npc);
    if (auto* weap = form->As<RE::TESObjectWEAP>())
        return static_cast<RE::TESFullName*>(weap);
    if (auto* armo = form->As<RE::TESObjectARMO>())
        return static_cast<RE::TESFullName*>(armo);
    return nullptr;
}

} // anonymous namespace

// ── Form Lookup ─────────────────────────────────────────────────────

extern "C" void* mora_rt_lookup_form(uint32_t formid) {
    return RE::TESForm::LookupByID(formid);
}

// ── Name write ──────────────────────────────────────────────────────

extern "C" void mora_rt_write_name(void* raw_form, const char* name) {
    if (!raw_form || !name) return;
    auto* named = get_full_name(static_cast<RE::TESForm*>(raw_form));
    if (named) {
        named->fullName = name;
    }
}

// ── Keyword add ─────────────────────────────────────────────────────

extern "C" void mora_rt_add_keyword(void* raw_form, void* raw_keyword) {
    if (!raw_form || !raw_keyword) return;
    auto* kf = get_keyword_form(static_cast<RE::TESForm*>(raw_form));
    auto* keyword = static_cast<RE::BGSKeyword*>(raw_keyword);
    if (kf && !kf->HasKeyword(keyword)) {
        kf->AddKeyword(keyword);
    }
}

// ── Keyword remove ──────────────────────────────────────────────────

extern "C" void mora_rt_remove_keyword(void* raw_form, void* raw_keyword) {
    if (!raw_form || !raw_keyword) return;
    auto* kf = get_keyword_form(static_cast<RE::TESForm*>(raw_form));
    auto* keyword = static_cast<RE::BGSKeyword*>(raw_keyword);
    if (kf) {
        kf->RemoveKeyword(keyword);
    }
}

// ── Spell add ───────────────────────────────────────────────────────

extern "C" void mora_rt_add_spell(void* raw_form, void* raw_spell) {
    if (!raw_form || !raw_spell) return;
    auto* npc = static_cast<RE::TESForm*>(raw_form)->As<RE::TESNPC>();
    auto* spell = static_cast<RE::SpellItem*>(raw_spell);
    if (!npc || !spell) return;

    auto* data = npc->GetSpellList();
    if (!data) return;

    // Idempotent
    for (std::uint32_t i = 0; i < data->numSpells; i++) {
        if (data->spells[i] == spell) return;
    }

    std::uint32_t newCount = data->numSpells + 1;
    auto* fresh = static_cast<RE::SpellItem**>(RE::malloc(newCount * sizeof(RE::SpellItem*)));
    if (!fresh) return;
    if (data->spells && data->numSpells > 0) {
        std::memcpy(fresh, data->spells, data->numSpells * sizeof(RE::SpellItem*));
    }
    fresh[data->numSpells] = spell;
    RE::free(data->spells);
    data->spells = fresh;
    data->numSpells = newCount;
}

// ── Spell remove ────────────────────────────────────────────────────

extern "C" void mora_rt_remove_spell(void* raw_form, void* raw_spell) {
    if (!raw_form || !raw_spell) return;
    auto* npc = static_cast<RE::TESForm*>(raw_form)->As<RE::TESNPC>();
    auto* spell = static_cast<RE::SpellItem*>(raw_spell);
    if (!npc || !spell) return;

    auto* data = npc->GetSpellList();
    if (!data) return;

    std::uint32_t found = UINT32_MAX;
    for (std::uint32_t i = 0; i < data->numSpells; i++) {
        if (data->spells[i] == spell) { found = i; break; }
    }
    if (found == UINT32_MAX) return;

    for (std::uint32_t i = found; i + 1 < data->numSpells; i++) {
        data->spells[i] = data->spells[i + 1];
    }
    data->numSpells--;
}

// ── Perk add ────────────────────────────────────────────────────────

extern "C" void mora_rt_add_perk(void* raw_form, void* raw_perk) {
    if (!raw_form || !raw_perk) return;
    auto* npc = static_cast<RE::TESForm*>(raw_form)->As<RE::TESNPC>();
    auto* perk = static_cast<RE::BGSPerk*>(raw_perk);
    if (!npc || !perk) return;

    // BGSPerkRankArray is a component on TESNPC via TESActorBase
    auto* perks = &npc->perks[0];
    std::uint32_t count = npc->perkCount;

    for (std::uint32_t i = 0; i < count; i++) {
        if (npc->perks[i].perk == perk) return;
    }

    std::uint32_t newCount = count + 1;
    auto* fresh = static_cast<RE::PerkRankData*>(
        RE::malloc(newCount * sizeof(RE::PerkRankData)));
    if (!fresh) return;
    if (count > 0) {
        std::memcpy(fresh, perks, count * sizeof(RE::PerkRankData));
    }
    fresh[count] = { perk, 0 };
    RE::free(perks);
    npc->perks = fresh;
    npc->perkCount = newCount;
}

// ── Faction add ─────────────────────────────────────────────────────

extern "C" void mora_rt_add_faction(void* raw_form, void* raw_faction) {
    if (!raw_form || !raw_faction) return;
    auto* npc = static_cast<RE::TESForm*>(raw_form)->As<RE::TESNPC>();
    auto* faction = static_cast<RE::TESFaction*>(raw_faction);
    if (!npc || !faction) return;

    for (auto& fr : npc->factions) {
        if (fr.faction == faction) return;
    }
    npc->factions.push_back({ faction, 0 });
}

// ── Shout add ───────────────────────────────────────────────────────

extern "C" void mora_rt_add_shout(void* raw_form, void* raw_shout) {
    if (!raw_form || !raw_shout) return;
    auto* npc = static_cast<RE::TESForm*>(raw_form)->As<RE::TESNPC>();
    auto* shout = static_cast<RE::TESShout*>(raw_shout);
    if (!npc || !shout) return;

    auto* data = npc->GetSpellList();
    if (!data) return;

    for (std::uint32_t i = 0; i < data->numShouts; i++) {
        if (data->shouts[i] == shout) return;
    }

    std::uint32_t newCount = data->numShouts + 1;
    auto* fresh = static_cast<RE::TESShout**>(RE::malloc(newCount * sizeof(RE::TESShout*)));
    if (!fresh) return;
    if (data->shouts && data->numShouts > 0) {
        std::memcpy(fresh, data->shouts, data->numShouts * sizeof(RE::TESShout*));
    }
    fresh[data->numShouts] = shout;
    RE::free(data->shouts);
    data->shouts = fresh;
    data->numShouts = newCount;
}

// ── Leveled list: add entry ─────────────────────────────────────────

extern "C" void mora_rt_add_to_leveled_list(void* raw_form,
                                              uint32_t entry_formid,
                                              uint16_t level, uint16_t count) {
    if (!raw_form) return;
    auto* form = static_cast<RE::TESForm*>(raw_form);

    RE::TESLeveledList* levList = nullptr;
    if (auto* li = form->As<RE::TESLevItem>())
        levList = static_cast<RE::TESLeveledList*>(li);
    else if (auto* lc = form->As<RE::TESLevCharacter>())
        levList = static_cast<RE::TESLeveledList*>(lc);
    if (!levList) return;

    auto* entryForm = RE::TESForm::LookupByID(entry_formid);
    if (!entryForm) return;

    // Duplicate check
    std::uint8_t numEntries = levList->numEntries;
    for (std::uint8_t i = 0; i < numEntries; i++) {
        if (levList->entries[i].form == entryForm &&
            levList->entries[i].level == level) return;
    }
    if (numEntries >= 255) return;

    std::uint8_t newCount = numEntries + 1;
    auto* fresh = static_cast<RE::LEVELED_OBJECT*>(
        RE::malloc(newCount * sizeof(RE::LEVELED_OBJECT)));
    if (!fresh) return;

    if (numEntries > 0) {
        std::memcpy(fresh, &levList->entries[0], numEntries * sizeof(RE::LEVELED_OBJECT));
    }

    std::memset(&fresh[numEntries], 0, sizeof(RE::LEVELED_OBJECT));
    fresh[numEntries].form = entryForm;
    fresh[numEntries].count = count;
    fresh[numEntries].level = level;

    // Insertion sort by level
    for (std::uint8_t i = 1; i < newCount; i++) {
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

    // Update the list — SimpleArray stores data pointer at +0x00, count externally
    RE::free(&levList->entries[0]);
    // Direct memory write to SimpleArray data pointer
    *reinterpret_cast<RE::LEVELED_OBJECT**>(
        reinterpret_cast<char*>(&levList->entries)) = fresh;
    levList->numEntries = newCount;
}

// ── Leveled list: remove ────────────────────────────────────────────

extern "C" void mora_rt_remove_from_leveled_list(void* raw_form, uint32_t entry_formid) {
    if (!raw_form) return;
    auto* form = static_cast<RE::TESForm*>(raw_form);

    RE::TESLeveledList* levList = nullptr;
    if (auto* li = form->As<RE::TESLevItem>())
        levList = static_cast<RE::TESLeveledList*>(li);
    else if (auto* lc = form->As<RE::TESLevCharacter>())
        levList = static_cast<RE::TESLeveledList*>(lc);
    if (!levList) return;

    std::uint8_t write = 0;
    for (std::uint8_t i = 0; i < levList->numEntries; i++) {
        std::uint32_t fid = levList->entries[i].form
            ? levList->entries[i].form->formID : 0;
        if (fid != entry_formid) {
            if (write != i) levList->entries[write] = levList->entries[i];
            write++;
        }
    }
    levList->numEntries = write;
}

// ── Leveled list: set chance none ───────────────────────────────────

extern "C" void mora_rt_set_chance_none(void* raw_form, int8_t chance) {
    if (!raw_form) return;
    auto* form = static_cast<RE::TESForm*>(raw_form);

    RE::TESLeveledList* levList = nullptr;
    if (auto* li = form->As<RE::TESLevItem>())
        levList = static_cast<RE::TESLeveledList*>(li);
    else if (auto* lc = form->As<RE::TESLevCharacter>())
        levList = static_cast<RE::TESLeveledList*>(lc);
    if (levList) {
        levList->chanceNone = chance;
    }
}

// ── Leveled list: clear ─────────────────────────────────────────────

extern "C" void mora_rt_clear_leveled_list(void* raw_form) {
    if (!raw_form) return;
    auto* form = static_cast<RE::TESForm*>(raw_form);

    RE::TESLeveledList* levList = nullptr;
    if (auto* li = form->As<RE::TESLevItem>())
        levList = static_cast<RE::TESLeveledList*>(li);
    else if (auto* lc = form->As<RE::TESLevCharacter>())
        levList = static_cast<RE::TESLeveledList*>(lc);
    if (!levList) return;

    if (levList->numEntries > 0) {
        levList->entries.clear();
    }
    levList->numEntries = 0;
}

#endif // _WIN32
