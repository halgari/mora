#include "mora/ast/types.h"

namespace mora {

bool MoraType::is_formid() const {
    switch (kind) {
        case TypeKind::FormID:
        case TypeKind::NpcID:
        case TypeKind::WeaponID:
        case TypeKind::ArmorID:
        case TypeKind::KeywordID:
        case TypeKind::FactionID:
        case TypeKind::SpellID:
        case TypeKind::PerkID:
        case TypeKind::QuestID:
        case TypeKind::LocationID:
        case TypeKind::CellID:
        case TypeKind::RaceID:
            return true;
        default:
            return false;
    }
}

bool MoraType::is_numeric() const {
    return kind == TypeKind::Int || kind == TypeKind::Float;
}

bool MoraType::is_subtype_of(const MoraType& parent) const {
    if (*this == parent) return true;
    // Any FormID subtype is a subtype of base FormID
    if (parent.kind == TypeKind::FormID && is_formid()) return true;
    return false;
}

std::string MoraType::to_string() const {
    if (kind == TypeKind::List) {
        std::string elem = "Unknown";
        if (element_type.has_value()) {
            elem = MoraType::make(*element_type).to_string();
        }
        return "List<" + elem + ">";
    }
    switch (kind) {
        case TypeKind::Int:       return "Int";
        case TypeKind::Float:     return "Float";
        case TypeKind::String:    return "String";
        case TypeKind::Bool:      return "Bool";
        case TypeKind::FormID:    return "FormID";
        case TypeKind::NpcID:     return "NpcID";
        case TypeKind::WeaponID:  return "WeaponID";
        case TypeKind::ArmorID:   return "ArmorID";
        case TypeKind::KeywordID: return "KeywordID";
        case TypeKind::FactionID: return "FactionID";
        case TypeKind::SpellID:   return "SpellID";
        case TypeKind::PerkID:    return "PerkID";
        case TypeKind::QuestID:   return "QuestID";
        case TypeKind::LocationID:return "LocationID";
        case TypeKind::CellID:    return "CellID";
        case TypeKind::RaceID:    return "RaceID";
        case TypeKind::Unknown:   return "Unknown";
        case TypeKind::Error:     return "Error";
        default:                  return "Unknown";
    }
}

} // namespace mora
