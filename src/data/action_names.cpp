#include "mora/data/action_names.h"

#include "mora/data/form_model.h"

#include <string_view>

namespace mora {

std::pair<FieldId, FieldOp> action_to_field(StringId         action_id,
                                              const StringPool& pool)
{
    std::string_view const name = pool.get(action_id);

    // Scalar fields: match set_action from the model
    for (size_t i = 0; i < model::kFieldCount; i++) {
        if (model::kFields[i].set_action && name == model::kFields[i].set_action)
            return {model::kFields[i].field_id, FieldOp::Set};
    }

    // Form array fields: match add_action and remove_action from the model
    for (size_t i = 0; i < model::kFormArrayCount; i++) {
        auto& fa = model::kFormArrays[i];
        if (fa.add_action && name == fa.add_action)
            return {fa.field_id, FieldOp::Add};
        if (fa.remove_action && name == fa.remove_action)
            return {fa.field_id, FieldOp::Remove};
    }

    // Boolean flags: match set_action
    for (size_t i = 0; i < model::kFlagCount; i++) {
        if (model::kFlags[i].set_action && name == model::kFlags[i].set_action)
            return {model::kFlags[i].field_id, FieldOp::Set};
    }

    // Scalar multiply (kept for backward compat during migration)
    using namespace mora::action;
    if (name == kMulDamage)        return {FieldId::Damage,       FieldOp::Multiply};
    if (name == kMulArmorRating)   return {FieldId::ArmorRating,  FieldOp::Multiply};
    if (name == kMulGoldValue)     return {FieldId::GoldValue,    FieldOp::Multiply};
    if (name == kMulWeight)        return {FieldId::Weight,       FieldOp::Multiply};
    if (name == kMulSpeed)         return {FieldId::Speed,        FieldOp::Multiply};
    if (name == kMulCritPercent)   return {FieldId::CritPercent,  FieldOp::Multiply};

    // Leveled list operations (special, not in scalar model)
    if (name == kAddToLeveledList)      return {FieldId::LeveledEntries, FieldOp::Add};
    if (name == kRemoveFromLeveledList) return {FieldId::LeveledEntries, FieldOp::Remove};
    if (name == kClearLeveledList)      return {FieldId::LeveledEntries, FieldOp::Set};

    // Legacy: add_item, add_lev_spell, set_game_setting, clear_all
    if (name == kAddItem)         return {FieldId::Items,       FieldOp::Add};
    if (name == kAddLevSpell)     return {FieldId::LevSpells,   FieldOp::Add};
    if (name == kSetGameSetting)  return {FieldId::GoldValue,   FieldOp::Set};
    if (name == kClearAll)        return {FieldId::ClearAll,    FieldOp::Set};

    // Unknown action
    return {FieldId::Invalid, FieldOp::Set};
}

} // namespace mora
