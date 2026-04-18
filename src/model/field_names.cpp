#include "mora/model/field_names.h"

#include <fmt/format.h>
#include <cstdint>

namespace mora {

std::string field_id_name(FieldId id) {
    switch (id) {
        case FieldId::Name:            return "Name";
        case FieldId::Damage:          return "Damage";
        case FieldId::ArmorRating:     return "ArmorRating";
        case FieldId::GoldValue:       return "GoldValue";
        case FieldId::Weight:          return "Weight";
        case FieldId::Keywords:        return "Keywords";
        case FieldId::Factions:        return "Factions";
        case FieldId::Perks:           return "Perks";
        case FieldId::Spells:          return "Spells";
        case FieldId::Items:           return "Items";
        case FieldId::Level:           return "Level";
        case FieldId::Race:            return "Race";
        case FieldId::EditorId:        return "EditorId";
        case FieldId::Shouts:          return "Shouts";
        case FieldId::LevSpells:       return "LevSpells";
        case FieldId::Speed:           return "Speed";
        case FieldId::Reach:           return "Reach";
        case FieldId::Stagger:         return "Stagger";
        case FieldId::RangeMin:        return "RangeMin";
        case FieldId::RangeMax:        return "RangeMax";
        case FieldId::CritDamage:      return "CritDamage";
        case FieldId::CritPercent:     return "CritPercent";
        case FieldId::Health:          return "Health";
        case FieldId::CalcLevelMin:    return "CalcLevelMin";
        case FieldId::CalcLevelMax:    return "CalcLevelMax";
        case FieldId::SpeedMult:       return "SpeedMult";
        case FieldId::RaceForm:        return "Race(form)";
        case FieldId::ClassForm:       return "Class";
        case FieldId::SkinForm:        return "Skin";
        case FieldId::OutfitForm:      return "Outfit";
        case FieldId::EnchantmentForm: return "Enchantment";
        case FieldId::VoiceTypeForm:   return "VoiceType";
        case FieldId::LeveledEntries:  return "LeveledEntries";
        case FieldId::ClearAll:        return "ClearAll";
        case FieldId::AutoCalcStats:   return "AutoCalcStats";
        case FieldId::Essential:       return "Essential";
        case FieldId::Protected:       return "Protected";
        default:                       return fmt::format("Field({})", static_cast<uint16_t>(id));
    }
}

} // namespace mora
