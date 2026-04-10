#pragma once
#include <string>
#include <optional>

namespace mora {

enum class TypeKind {
    Int, Float, String, Bool,
    FormID,     // base FormID type
    NpcID, WeaponID, ArmorID, KeywordID, FactionID,
    SpellID, PerkID, QuestID, LocationID, CellID, RaceID,
    List,       // List<T>
    Unknown,    // not yet inferred
    Error,      // type error sentinel
};

struct MoraType {
    TypeKind kind;
    std::optional<TypeKind> element_type; // for List<T>

    bool operator==(const MoraType& other) const = default;
    static MoraType make(TypeKind k) { return {k, std::nullopt}; }
    static MoraType make_list(TypeKind elem) { return {TypeKind::List, elem}; }

    bool is_formid() const;
    bool is_numeric() const;
    bool is_subtype_of(const MoraType& parent) const;
    std::string to_string() const;
};

} // namespace mora
