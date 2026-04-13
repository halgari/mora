#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// SkyPatcher Schema — Data-driven record type definitions
// ═══════════════════════════════════════════════════════════════════════════
//
// Each record type (weapon, armor, NPC, etc.) is defined as a constexpr
// table of filter keys and operation keys. One generic parser handles all
// record types by dispatching through these tables.
//
// Adding a new record type = add one schema table. Zero new parsing code.
// ═══════════════════════════════════════════════════════════════════════════

#include <array>
#include <cstdint>
#include <string_view>

namespace mora {

// ── Filter kinds ─────────────────────────────────────────────────────

enum class FilterKind : uint8_t {
    DirectTarget,       // filterByWeapons=FormRef,FormRef — patch only these
    DirectExclude,      // filterByWeaponsExcluded=FormRef — exclude these
    KeywordAnd,         // filterByKeywords=KW1,KW2 — must have ALL
    KeywordOr,          // filterByKeywordsOr=KW1,KW2 — must have ANY
    KeywordExclude,     // filterByKeywordsExcluded=KW1 — must NOT have
    FactionAnd,         // filterByFactions=F1,F2 — must have ALL
    FactionOr,          // filterByFactionsOr=F1,F2 — must have ANY
    FactionExclude,     // filterByFactionsExcluded=F1 — must NOT have
    RaceOr,             // filterByRaces=R1,R2 — must be one of these races
    EditorIdAnd,        // filterByEditorIdContains=str — must contain ALL
    EditorIdOr,         // filterByEditorIdContainsOr=str — must contain ANY
    EditorIdExclude,    // filterByEditorIdContainsExcluded=str — must NOT contain
    GenderFilter,       // filterByGender=male/female
    SourcePlugin,       // filterByModNames=Plugin.esp — from these plugins
    PluginRequired,     // hasPlugins=P1,P2 — all must be installed (AND)
    PluginRequiredOr,   // hasPluginsOr=P1,P2 — at least one (OR)
    LevelRange,         // levelRange=min~max
};

// ── Operation kinds ──────────────────────────────────────────────────

enum class OpKind : uint8_t {
    SetInt,             // attackDamage=30
    AddInt,             // attackDamageToAdd=5
    MulFloat,           // attackDamageMult=1.5
    SetFloat,           // speed=1.2, weight=5.0
    SetName,            // fullName=~New Name~
    AddFormList,        // keywordsToAdd=KW1,KW2 / spellsToAdd=S1,S2
    RemoveFormList,     // keywordsToRemove=KW1
    SetForm,            // objectEffect=Skyrim.esm|0x12345, race=NordRace
    ClearFlag,          // clear=true
    SetBool,            // setAutoCalcStats=true
    AddToLeveledList,   // addToLLs=Form~level~count,...
    RemoveFromLeveledList, // removeFromLLs=Form~level~count
};

// ── Field IDs (extended beyond patch_set.h for SkyPatcher coverage) ──

enum class SkyField : uint16_t {
    // Scalar values (map to existing FieldId where possible)
    Damage = 2, ArmorRating = 3, GoldValue = 4, Weight = 5,
    Name = 1,
    Speed = 20, Reach = 21, Stagger = 22, RangeMin = 23, RangeMax = 24,
    CritDamage = 25, CritPercent = 26,
    Health = 30,
    Level = 40, CalcLevelMin = 41, CalcLevelMax = 42,
    SpeedMult = 43,

    // Form list operations
    Keywords = 6, Spells = 8, Perks = 9, Factions = 7, Items = 10,
    LevSpells = 11, Shouts = 12,

    // Form references
    Race = 50, Class = 51, Skin = 52, OutfitDefault = 53,
    Enchantment = 54, VoiceType = 55,

    // Leveled list
    LeveledEntries = 60,
    ChanceNone = 61,

    // Flags
    ClearAll = 70,
    AutoCalcStats = 71, Essential = 72, Protected = 73,
};

// ── Schema entry types ───────────────────────────────────────────────

struct FilterDef {
    std::string_view key;
    FilterKind kind;
};

struct OpDef {
    std::string_view key;
    OpKind kind;
    SkyField field;
};

// ── Record schema ────────────────────────────────────────────────────
// Constexpr tables — no heap allocation, no virtual dispatch.

struct RecordSchema {
    std::string_view type_name;     // "weapon", "armor", "npc", etc.
    std::string_view mora_relation; // Mora fact name for existence check

    const FilterDef* filters;
    size_t filter_count;

    const OpDef* operations;
    size_t op_count;

    // Lookup by key (linear scan — tables are small, this is compile-time-hot)
    const FilterDef* find_filter(std::string_view key) const {
        for (size_t i = 0; i < filter_count; i++) {
            if (filters[i].key == key) return &filters[i];
        }
        return nullptr;
    }

    const OpDef* find_operation(std::string_view key) const {
        for (size_t i = 0; i < op_count; i++) {
            if (operations[i].key == key) return &operations[i];
        }
        return nullptr;
    }
};

// Helper macro for defining schemas concisely
#define MORA_SCHEMA_FILTERS(name, ...) \
    constexpr FilterDef name[] = { __VA_ARGS__ }
#define MORA_SCHEMA_OPS(name, ...) \
    constexpr OpDef name[] = { __VA_ARGS__ }
#define MORA_SCHEMA(var, type, relation, filters, ops) \
    constexpr RecordSchema var = { type, relation, filters, std::size(filters), ops, std::size(ops) }

// ═══════════════════════════════════════════════════════════════════════════
// Schema definitions for each record type
// ═══════════════════════════════════════════════════════════════════════════

namespace schemas {

// ── Common filter keys (reused across record types) ──────────────────

// ── Weapon ───────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(weapon_filters,
    {"filterbyweapons",               FilterKind::DirectTarget},
    {"filterbyweaponsexcluded",       FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"filterbymodnames",              FilterKind::SourcePlugin},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(weapon_ops,
    {"attackdamage",       OpKind::SetInt,       SkyField::Damage},
    {"attackdamagetoadd",  OpKind::AddInt,       SkyField::Damage},
    {"attackdamagemult",   OpKind::MulFloat,     SkyField::Damage},
    {"speed",              OpKind::SetFloat,     SkyField::Speed},
    {"speedmult",          OpKind::MulFloat,     SkyField::Speed},
    {"reach",              OpKind::SetFloat,     SkyField::Reach},
    {"stagger",            OpKind::SetFloat,     SkyField::Stagger},
    {"rangemin",           OpKind::SetFloat,     SkyField::RangeMin},
    {"rangemax",           OpKind::SetFloat,     SkyField::RangeMax},
    {"weight",             OpKind::SetFloat,     SkyField::Weight},
    {"weightmult",         OpKind::MulFloat,     SkyField::Weight},
    {"value",              OpKind::SetInt,       SkyField::GoldValue},
    {"valuemult",          OpKind::MulFloat,     SkyField::GoldValue},
    {"critdamage",         OpKind::SetInt,       SkyField::CritDamage},
    {"critpercentmult",    OpKind::SetFloat,     SkyField::CritPercent},
    {"fullname",           OpKind::SetName,      SkyField::Name},
    {"keywordstoadd",      OpKind::AddFormList,  SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
    {"objecteffect",       OpKind::SetForm,      SkyField::Enchantment},
);

MORA_SCHEMA(weapon, "weapon", "weapon", weapon_filters, weapon_ops);

// ── Armor ────────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(armor_filters,
    {"filterbyarmors",                FilterKind::DirectTarget},
    {"filterbyarmorsexcluded",        FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"filterbymodnames",              FilterKind::SourcePlugin},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(armor_ops,
    {"damageresist",       OpKind::SetInt,       SkyField::ArmorRating},
    {"damageresistmultiply", OpKind::MulFloat,   SkyField::ArmorRating},
    {"health",             OpKind::SetInt,       SkyField::Health},
    {"weight",             OpKind::SetFloat,     SkyField::Weight},
    {"weightmult",         OpKind::MulFloat,     SkyField::Weight},
    {"value",              OpKind::SetInt,       SkyField::GoldValue},
    {"valuemult",          OpKind::MulFloat,     SkyField::GoldValue},
    {"fullname",           OpKind::SetName,      SkyField::Name},
    {"keywordstoadd",      OpKind::AddFormList,  SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
    {"objecteffect",       OpKind::SetForm,      SkyField::Enchantment},
);

MORA_SCHEMA(armor, "armor", "armor", armor_filters, armor_ops);

// ── NPC ──────────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(npc_filters,
    {"filterbynpcs",                  FilterKind::DirectTarget},
    {"filterbynpcsexcluded",          FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyraces",                 FilterKind::RaceOr},
    {"filterbyfactions",              FilterKind::FactionAnd},
    {"filterbyfactionsor",            FilterKind::FactionOr},
    {"filterbyfactionsexcluded",      FilterKind::FactionExclude},
    {"filterbygender",                FilterKind::GenderFilter},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"filterbymodnames",              FilterKind::SourcePlugin},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
    {"levelrange",                    FilterKind::LevelRange},
);

MORA_SCHEMA_OPS(npc_ops,
    {"level",              OpKind::SetInt,       SkyField::Level},
    {"calclevelmin",       OpKind::SetInt,       SkyField::CalcLevelMin},
    {"calclevelmax",       OpKind::SetInt,       SkyField::CalcLevelMax},
    {"fullname",           OpKind::SetName,      SkyField::Name},
    {"keywordstoadd",      OpKind::AddFormList,  SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
    {"spellstoadd",        OpKind::AddFormList,  SkyField::Spells},
    {"spellstoremove",     OpKind::RemoveFormList, SkyField::Spells},
    {"perkstoadd",         OpKind::AddFormList,  SkyField::Perks},
    {"shoutstoadd",        OpKind::AddFormList,  SkyField::Shouts},
    {"shoutstoremove",     OpKind::RemoveFormList, SkyField::Shouts},
    {"race",               OpKind::SetForm,      SkyField::Race},
    {"class",              OpKind::SetForm,      SkyField::Class},
    {"skin",               OpKind::SetForm,      SkyField::Skin},
    {"outfitdefault",      OpKind::SetForm,      SkyField::OutfitDefault},
    {"voicetype",          OpKind::SetForm,      SkyField::VoiceType},
    {"setessential",       OpKind::SetBool,      SkyField::Essential},
    {"setprotected",       OpKind::SetBool,      SkyField::Protected},
    {"setautocalcstats",   OpKind::SetBool,      SkyField::AutoCalcStats},
);

MORA_SCHEMA(npc, "npc", "npc", npc_filters, npc_ops);

// ── Leveled List ─────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(leveled_list_filters,
    {"filterbylls",                   FilterKind::DirectTarget},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
);

MORA_SCHEMA_OPS(leveled_list_ops,
    {"addtolls",           OpKind::AddToLeveledList,      SkyField::LeveledEntries},
    {"addoncetolls",       OpKind::AddToLeveledList,      SkyField::LeveledEntries},
    {"removefromlls",      OpKind::RemoveFromLeveledList, SkyField::LeveledEntries},
    {"clear",              OpKind::ClearFlag,              SkyField::ClearAll},
    {"chancenone",         OpKind::SetInt,                 SkyField::ChanceNone},
);

MORA_SCHEMA(leveled_list, "leveledList", "leveled_list", leveled_list_filters, leveled_list_ops);

// ── Magic Effect ─────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(magic_effect_filters,
    {"filterbymgefs",                 FilterKind::DirectTarget},
    {"filterbymgefsexcluded",         FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"filterbymodnames",              FilterKind::SourcePlugin},
);

MORA_SCHEMA_OPS(magic_effect_ops,
    {"fullname",           OpKind::SetName,      SkyField::Name},
    {"basecost",           OpKind::SetFloat,     SkyField::GoldValue},
    {"keywordstoadd",      OpKind::AddFormList,  SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(magic_effect, "magicEffect", "magic_effect", magic_effect_filters, magic_effect_ops);

// ── Ammo ────────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(ammo_filters,
    {"filterbyammo",                  FilterKind::DirectTarget},
    {"filterbyammoexcluded",          FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(ammo_ops,
    {"damage",             OpKind::SetInt,        SkyField::Damage},
    {"weight",             OpKind::SetFloat,      SkyField::Weight},
    {"value",              OpKind::SetInt,        SkyField::GoldValue},
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"keywordstoadd",      OpKind::AddFormList,   SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(ammo, "ammo", "ammo", ammo_filters, ammo_ops);

// ── Spell ───────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(spell_filters,
    {"filterbyspells",                FilterKind::DirectTarget},
    {"filterbyspellsexcluded",        FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(spell_ops,
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"basecost",           OpKind::SetFloat,      SkyField::GoldValue},
    {"keywordstoadd",      OpKind::AddFormList,   SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(spell_schema, "spell", "spell", spell_filters, spell_ops);

// ── Enchantment ─────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(enchantment_filters,
    {"filterbyenchantments",          FilterKind::DirectTarget},
    {"filterbyenchantmentsexcluded",  FilterKind::DirectExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(enchantment_ops,
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"basecost",           OpKind::SetFloat,      SkyField::GoldValue},
);

MORA_SCHEMA(enchantment, "enchantment", "enchantment", enchantment_filters, enchantment_ops);

// ── Potion (alchemyItem) ────────────────────────────────────────────

MORA_SCHEMA_FILTERS(potion_filters,
    {"filterbypotions",               FilterKind::DirectTarget},
    {"filterbypotionsexcluded",       FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"filterbyeditoridcontainsor",    FilterKind::EditorIdOr},
    {"filterbyeditoridcontainsexcluded", FilterKind::EditorIdExclude},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(potion_ops,
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"weight",             OpKind::SetFloat,      SkyField::Weight},
    {"value",              OpKind::SetInt,        SkyField::GoldValue},
    {"keywordstoadd",      OpKind::AddFormList,   SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(potion, "alchemyItem", "potion", potion_filters, potion_ops);

// ── Ingredient ──────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(ingredient_filters,
    {"filterbyingredients",           FilterKind::DirectTarget},
    {"filterbyingredientsexcluded",   FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(ingredient_ops,
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"weight",             OpKind::SetFloat,      SkyField::Weight},
    {"value",              OpKind::SetInt,        SkyField::GoldValue},
    {"keywordstoadd",      OpKind::AddFormList,   SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(ingredient, "ingredient", "ingredient", ingredient_filters, ingredient_ops);

// ── Book ────────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(book_filters,
    {"filterbybooks",                 FilterKind::DirectTarget},
    {"filterbybooksexcluded",         FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(book_ops,
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"weight",             OpKind::SetFloat,      SkyField::Weight},
    {"value",              OpKind::SetInt,        SkyField::GoldValue},
    {"keywordstoadd",      OpKind::AddFormList,   SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(book, "book", "book", book_filters, book_ops);

// ── Scroll ──────────────────────────────────────────────────────────

MORA_SCHEMA_FILTERS(scroll_filters,
    {"filterbyscrolls",               FilterKind::DirectTarget},
    {"filterbyscrollsexcluded",       FilterKind::DirectExclude},
    {"filterbykeywords",              FilterKind::KeywordAnd},
    {"filterbykeywordsor",            FilterKind::KeywordOr},
    {"filterbykeywordsexcluded",      FilterKind::KeywordExclude},
    {"filterbyeditoridcontains",      FilterKind::EditorIdAnd},
    {"hasplugins",                    FilterKind::PluginRequired},
    {"haspluginsor",                  FilterKind::PluginRequiredOr},
);

MORA_SCHEMA_OPS(scroll_ops,
    {"fullname",           OpKind::SetName,       SkyField::Name},
    {"weight",             OpKind::SetFloat,      SkyField::Weight},
    {"value",              OpKind::SetInt,        SkyField::GoldValue},
    {"keywordstoadd",      OpKind::AddFormList,   SkyField::Keywords},
    {"keywordstoremove",   OpKind::RemoveFormList, SkyField::Keywords},
);

MORA_SCHEMA(scroll, "scroll", "scroll", scroll_filters, scroll_ops);

// ── Schema registry ──────────────────────────────────────────────────

constexpr const RecordSchema* all_schemas[] = {
    &weapon, &armor, &npc, &leveled_list, &magic_effect,
    &ammo, &spell_schema, &enchantment, &potion, &ingredient, &book, &scroll,
};

constexpr size_t schema_count = std::size(all_schemas);

inline const RecordSchema* find_schema(std::string_view type_name) {
    for (size_t i = 0; i < schema_count; i++) {
        if (all_schemas[i]->type_name == type_name) return all_schemas[i];
    }
    return nullptr;
}

} // namespace schemas
} // namespace mora
