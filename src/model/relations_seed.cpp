#include "mora/model/relations.h"
#include "mora/model/validate.h"

namespace mora::model {

constexpr RelationEntry kRelations[] = {
    // ── Type predicates (unary, read-only from ESP) ────────────────────
    { .namespace_ = "form", .name = "npc",
      .args = {{RelValueType::FormRef, "F"}}, .arg_count = 1,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_"},
      .docs = "True when F is a base NPC record." },

    { .namespace_ = "form", .name = "weapon",
      .args = {{RelValueType::FormRef, "F"}}, .arg_count = 1,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "WEAP"},
      .docs = "True when F is a weapon base record." },

    { .namespace_ = "form", .name = "armor",
      .args = {{RelValueType::FormRef, "F"}}, .arg_count = 1,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "ARMO"},
      .docs = "True when F is an armor base record." },

    // ── Set-valued static relations ────────────────────────────────────
    { .namespace_ = "form", .name = "keyword",
      .args = {{RelValueType::FormRef, "F"}, {RelValueType::FormRef, "KW"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "", .subrecord = "KWDA"},
      .docs = "Keyword membership on a base record (body: query; head: add/remove)." },

    { .namespace_ = "form", .name = "faction",
      .args = {{RelValueType::FormRef, "NPC"}, {RelValueType::FormRef, "FAC"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_", .subrecord = "SNAM"},
      .docs = "Faction membership on an NPC base record." },

    // ── Countable numeric scalars ──────────────────────────────────────
    { .namespace_ = "form", .name = "damage",
      .args = {{RelValueType::FormRef, "W"}, {RelValueType::Int, "N"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "WEAP", .subrecord = "DNAM"},
      .docs = "Weapon base damage." },

    { .namespace_ = "form", .name = "gold_value",
      .args = {{RelValueType::FormRef, "F"}, {RelValueType::Int, "N"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Static,
      .esp_source = {.subrecord = "DATA"},
      .docs = "Gold value of an item." },

    // ── Scalar (set-only) ──────────────────────────────────────────────
    { .namespace_ = "form", .name = "name",
      .args = {{RelValueType::FormRef, "F"}, {RelValueType::String, "S"}}, .arg_count = 2,
      .value_type = RelValueType::String, .cardinality = Cardinality::Scalar,
      .source = RelationSourceKind::Static,
      .esp_source = {.subrecord = "FULL"},
      .docs = "Display name." },

    { .namespace_ = "form", .name = "base_level",
      .args = {{RelValueType::FormRef, "NPC"}, {RelValueType::Int, "L"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_", .subrecord = "ACBS"},
      .docs = "NPC base level." },

    { .namespace_ = "form", .name = "race",
      .args = {{RelValueType::FormRef, "NPC"}, {RelValueType::FormRef, "RACE"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Static,
      .esp_source = {.record_type = "NPC_", .subrecord = "RNAM"},
      .docs = "NPC base race." },

    // ── ref/* dynamic relations ────────────────────────────────────────
    { .namespace_ = "ref", .name = "keyword",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "KW"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Handler,
      .apply_handler   = HandlerId::RefAddKeyword,
      .retract_handler = HandlerId::RefRemoveKeyword,
      .docs = "Keywords added dynamically to a specific placed reference." },

    { .namespace_ = "ref", .name = "current_location",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "LOC"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Handler,
      .docs = "The location this reference is currently in." },

    { .namespace_ = "ref", .name = "base_form",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "F"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Handler,
      .docs = "Bridge from live reference to its base record." },

    { .namespace_ = "ref", .name = "in_combat",
      .args = {{RelValueType::FormRef, "R"}}, .arg_count = 1,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Handler,
      .docs = "True when this reference is currently in combat." },

    // ── player/* relations ────────────────────────────────────────────
    { .namespace_ = "player", .name = "gold",
      .args = {{RelValueType::FormRef, "P"}, {RelValueType::Int, "N"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Countable,
      .source = RelationSourceKind::Handler,
      .apply_handler = HandlerId::PlayerAddGold,
      .docs = "Player gold count (additive delta via 'add')." },

    { .namespace_ = "player", .name = "notification",
      .args = {{RelValueType::FormRef, "P"}, {RelValueType::String, "S"}}, .arg_count = 2,
      .value_type = RelValueType::String, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Handler,
      .apply_handler = HandlerId::PlayerShowNotification,
      .docs = "Queue a UI notification string." },

    // ── world/* globals ────────────────────────────────────────────
    { .namespace_ = "world", .name = "time_of_day",
      .args = {{RelValueType::Float, "T"}}, .arg_count = 1,
      .value_type = RelValueType::Float, .cardinality = Cardinality::Functional,
      .source = RelationSourceKind::Hook,
      .hook = {"OnTimeOfDayChanged", HookSpec::Kind::State},
      .docs = "Current in-game hour 0..24." },

    // ── event/* edge-triggered inputs ────────────────────────────
    { .namespace_ = "event", .name = "entered_location",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::FormRef, "LOC"}}, .arg_count = 2,
      .value_type = RelValueType::FormRef, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Event,
      .hook = {"OnLocationChange", HookSpec::Kind::Edge},
      .docs = "Fires when a reference enters a new location." },

    { .namespace_ = "event", .name = "combat_state_changed",
      .args = {{RelValueType::FormRef, "R"}, {RelValueType::Int, "STATE"}}, .arg_count = 2,
      .value_type = RelValueType::Int, .cardinality = Cardinality::Set,
      .source = RelationSourceKind::Event,
      .hook = {"OnCombatStateChanged", HookSpec::Kind::Edge},
      .docs = "Fires on combat state transitions." },
};

const size_t kRelationCount = sizeof(kRelations) / sizeof(kRelations[0]);

static_assert(validate_all(kRelations, sizeof(kRelations) / sizeof(kRelations[0])),
              "kRelations fails validation — see individual helper checks");

} // namespace mora::model
