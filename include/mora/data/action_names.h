#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// Shared constants for action names, relation names, and record type tags.
// Single source of truth — used by evaluator, parser, schema, pipeline.
// ═══════════════════════════════════════════════════════════════════════════

#include "mora/eval/field_types.h"
#include "mora/core/string_pool.h"
#include <cstdint>
#include <utility>

namespace mora {

// ── Helpers for FieldId/FieldOp in switch cases ─────────────────────
// PatchEntry stores field_id as uint8_t and op as uint8_t.
// These helpers make switch cases readable without casts at every call site.

constexpr uint8_t fid(FieldId id) { return static_cast<uint8_t>(id); }
constexpr uint8_t fop(FieldOp op) { return static_cast<uint8_t>(op); }
constexpr uint16_t fid16(FieldId id) { return static_cast<uint16_t>(id); }

// ── Patch table format constants ────────────────────────────────────

constexpr uint32_t kPatchTableMagic   = 0x4D4F5241; // "MORA" in ASCII
constexpr uint32_t kPatchTableVersion = 3;
constexpr size_t   kPatchStringBufSize = 512;

// ── NPC ACBS flag bits ──────────────────────────────────────────────

namespace npc_flags {
    constexpr uint32_t kFemale       = 1 << 0;
    constexpr uint32_t kEssential    = 1 << 1;
    constexpr uint32_t kProtected    = 1 << 11;
    constexpr uint32_t kAutoCalc     = 1 << 4;
} // namespace npc_flags

// ── TESForm layout ──────────────────────────────────────────────────

constexpr uint64_t kFormTypeOffset = 0x1A; // offset of formType byte in TESForm

// ── Action name constants ───────────────────────────────────────────
// Used by rule_planner.cpp (action_to_field) and skypatcher_parser.cpp (sky_field_to_action)

namespace action {
    // Form list add
    constexpr const char* kAddKeyword   = "add_keyword";
    constexpr const char* kAddSpell     = "add_spell";
    constexpr const char* kAddPerk      = "add_perk";
    constexpr const char* kAddFaction   = "add_faction";
    constexpr const char* kAddShout     = "add_shout";
    constexpr const char* kAddItem      = "add_item";
    constexpr const char* kAddLevSpell  = "add_lev_spell";

    // Form list remove
    constexpr const char* kRemoveKeyword = "remove_keyword";
    constexpr const char* kRemoveSpell   = "remove_spell";
    constexpr const char* kRemoveFaction = "remove_faction";
    constexpr const char* kRemoveShout   = "remove_shout";

    // Scalar set
    constexpr const char* kSetName         = "set_name";
    constexpr const char* kSetDamage       = "set_damage";
    constexpr const char* kSetArmorRating  = "set_armor_rating";
    constexpr const char* kSetGoldValue    = "set_gold_value";
    constexpr const char* kSetWeight       = "set_weight";
    constexpr const char* kSetSpeed        = "set_speed";
    constexpr const char* kSetReach        = "set_reach";
    constexpr const char* kSetStagger      = "set_stagger";
    constexpr const char* kSetRangeMin     = "set_range_min";
    constexpr const char* kSetRangeMax     = "set_range_max";
    constexpr const char* kSetCritDamage   = "set_crit_damage";
    constexpr const char* kSetCritPercent  = "set_crit_percent";
    constexpr const char* kSetHealth       = "set_health";
    constexpr const char* kSetLevel        = "set_level";
    constexpr const char* kSetCalcLevelMin = "set_calc_level_min";
    constexpr const char* kSetCalcLevelMax = "set_calc_level_max";
    constexpr const char* kSetSpeedMult    = "set_speed_mult";
    constexpr const char* kSetGameSetting  = "set_game_setting";

    // Scalar multiply
    constexpr const char* kMulDamage      = "mul_damage";
    constexpr const char* kMulArmorRating = "mul_armor_rating";
    constexpr const char* kMulGoldValue   = "mul_gold_value";
    constexpr const char* kMulWeight      = "mul_weight";
    constexpr const char* kMulSpeed       = "mul_speed";
    constexpr const char* kMulCritPercent = "mul_crit_percent";

    // Form references
    constexpr const char* kSetRace       = "set_race";
    constexpr const char* kSetClass      = "set_class";
    constexpr const char* kSetSkin       = "set_skin";
    constexpr const char* kSetOutfit     = "set_outfit";
    constexpr const char* kSetEnchantment = "set_enchantment";
    constexpr const char* kSetVoiceType  = "set_voice_type";

    // Boolean flags
    constexpr const char* kSetEssential     = "set_essential";
    constexpr const char* kSetProtected     = "set_protected";
    constexpr const char* kSetAutoCalcStats = "set_auto_calc_stats";
    constexpr const char* kClearAll         = "clear_all";

    // Leveled list
    constexpr const char* kAddToLeveledList      = "add_to_leveled_list";
    constexpr const char* kRemoveFromLeveledList = "remove_from_leveled_list";
    constexpr const char* kSetChanceNone         = "set_chance_none";
    constexpr const char* kClearLeveledList      = "clear_leveled_list";
} // namespace action

// ── Relation name constants ─────────────────────────────────────────
// Used by schema_registry, main.cpp, parsers, pipeline_evaluator

namespace rel {
    // Record existence relations
    constexpr const char* kNpc         = "npc";
    constexpr const char* kWeapon      = "weapon";
    constexpr const char* kArmor       = "armor";
    constexpr const char* kAmmo        = "ammo";
    constexpr const char* kPotion      = "potion";
    constexpr const char* kBook        = "book";
    constexpr const char* kSpell       = "spell";
    constexpr const char* kMiscItem    = "misc_item";
    constexpr const char* kMagicEffect = "magic_effect";
    constexpr const char* kIngredient  = "ingredient";
    constexpr const char* kScroll      = "scroll";
    constexpr const char* kSoulGem     = "soul_gem";
    constexpr const char* kEnchantment = "enchantment";
    constexpr const char* kPerk        = "perk";
    constexpr const char* kKeyword     = "keyword";
    constexpr const char* kFaction     = "faction";
    constexpr const char* kRace        = "race";
    constexpr const char* kLeveledList = "leveled_list";

    // Derived/join relations
    constexpr const char* kHasKeyword   = "has_keyword";
    constexpr const char* kHasFaction   = "has_faction";
    constexpr const char* kHasPerk      = "has_perk";
    constexpr const char* kHasSpell     = "has_spell";
    constexpr const char* kRaceOf       = "race_of";
    constexpr const char* kBaseLevel    = "base_level";
    constexpr const char* kEditorId     = "editor_id";
    constexpr const char* kName         = "name";
    constexpr const char* kDamage       = "damage";
    constexpr const char* kGoldValue    = "gold_value";
    constexpr const char* kWeight       = "weight";
    constexpr const char* kArmorRating  = "armor_rating";

    // SkyPatcher-specific
    constexpr const char* kPluginLoaded = "plugin_loaded";
    constexpr const char* kNpcGender    = "npc_gender";
    constexpr const char* kNpcFlags     = "npc_flags";
    constexpr const char* kFormSource   = "form_source";

    // SPID/KID distribution
    constexpr const char* kSpidDist     = "spid_dist";
    constexpr const char* kKidDist      = "kid_dist";
    constexpr const char* kSpidFilter   = "spid_filter";
    constexpr const char* kKidFilter    = "kid_filter";
} // namespace rel

// ── ESP Record type tags (4CC codes) ────────────────────────────────
// Used by schema_registry.cpp for ESP data extraction

namespace rec {
    constexpr const char* kNPC_  = "NPC_";
    constexpr const char* kWEAP  = "WEAP";
    constexpr const char* kARMO  = "ARMO";
    constexpr const char* kAMMO  = "AMMO";
    constexpr const char* kALCH  = "ALCH";
    constexpr const char* kBOOK  = "BOOK";
    constexpr const char* kSPEL  = "SPEL";
    constexpr const char* kPERK  = "PERK";
    constexpr const char* kKYWD  = "KYWD";
    constexpr const char* kFACT  = "FACT";
    constexpr const char* kRACE  = "RACE";
    constexpr const char* kLVLI  = "LVLI";
    constexpr const char* kINGR  = "INGR";
    constexpr const char* kSCRL  = "SCRL";
    constexpr const char* kENCH  = "ENCH";
    constexpr const char* kMGEF  = "MGEF";
    constexpr const char* kMISC  = "MISC";
    constexpr const char* kSLGM  = "SLGM";
    constexpr const char* kCONT  = "CONT";

    // Subrecord tags
    constexpr const char* kKWDA = "KWDA";
    constexpr const char* kFULL = "FULL";
    constexpr const char* kSNAM = "SNAM";
    constexpr const char* kSPLO = "SPLO";
    constexpr const char* kPRKR = "PRKR";
    constexpr const char* kDATA = "DATA";
    constexpr const char* kDNAM = "DNAM";
    constexpr const char* kACBS = "ACBS";
    constexpr const char* kRNAM = "RNAM";
} // namespace rec

// ── Gender string values ────────────────────────────────────────────

namespace gender {
    constexpr const char* kMale   = "male";
    constexpr const char* kFemale = "female";
} // namespace gender

// ── Free function: action name → (FieldId, FieldOp) ────────────────
// Maps a fully-assembled action name (e.g. "set_gold_value") to the
// corresponding (FieldId, FieldOp) pair. Returns {FieldId::Invalid,
// FieldOp::Set} when the name is not recognised. Used by RulePlanner.
std::pair<FieldId, FieldOp> action_to_field(StringId    action_id,
                                             const StringPool& pool);

} // namespace mora
