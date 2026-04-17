// Generates markdown documentation from the form model.
// Used by: mora docs

#include "mora/cli/doc_generator.h"
#include "mora/data/action_names.h"
#include "mora/data/form_model.h"
#include <fmt/format.h>
#include <iostream>
#include <string>

namespace mora {

namespace {

const char* type_kind_name(TypeKind tk) {
    switch (tk) {
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
        case TypeKind::RaceID:    return "RaceID";
        default:                  return "Unknown";
    }
}

std::string applicable_types(uint8_t comp_idx) {
    return model::form_types_with_component(comp_idx);
}

} // anonymous namespace

void generate_docs(std::ostream& out) {
    namespace m = model;

    out << "# Language Reference\n\n";
    out << "This reference is auto-generated from the Mora form model.\n";
    out << "For a guided introduction, see the [Language Guide](language-guide.md).\n\n";
    out << "---\n\n";

    // ── Types ──
    out << "## Types\n\n";
    out << "| Type | Description |\n";
    out << "|------|-------------|\n";
    out << "| `FormID` | Any game record |\n";
    out << "| `WeaponID` | Weapon record (from `weapon(W)`) |\n";
    out << "| `ArmorID` | Armor record (from `armor(A)`) |\n";
    out << "| `NpcID` | NPC record (from `npc(N)`) |\n";
    out << "| `SpellID` | Spell record |\n";
    out << "| `PerkID` | Perk record |\n";
    out << "| `KeywordID` | Keyword record |\n";
    out << "| `FactionID` | Faction record |\n";
    out << "| `RaceID` | Race record |\n";
    out << "| `String` | Text value |\n";
    out << "| `Int` | Integer |\n";
    out << "| `Float` | Decimal |\n";
    out << "\n---\n\n";

    // ── Form Relations ──
    out << "## Form Relations\n\n";
    out << "Form relations enumerate what records exist in the load order.\n\n";
    out << "| Relation | Signature |\n";
    out << "|----------|-----------|\n";
    for (size_t i = 0; i < m::kFormTypeCount; i++) {
        auto& ft = *m::kFormTypes[i];
        out << fmt::format("| `{}` | `({})` |\n",
            ft.relation_name, type_kind_name(ft.type_kind));
    }
    for (size_t i = 0; i < m::kExistenceOnlyCount; i++) {
        auto& eo = m::kExistenceOnly[i];
        out << fmt::format("| `{}` | `({})` |\n",
            eo.relation_name, type_kind_name(eo.type_kind));
    }
    out << "\n---\n\n";

    // ── Property Relations ──
    out << "## Property Relations\n\n";
    out << "Property relations expose attributes of a form for filtering and binding.\n\n";
    out << "| Relation | Signature | Applies to |\n";
    out << "|----------|-----------|------------|\n";

    // Scalar fields with relation names
    for (size_t i = 0; i < m::kFieldCount; i++) {
        auto& f = m::kFields[i];
        if (!f.relation_name) continue;
        auto& member = m::kComponents[f.component_idx].members[f.member_idx];
        auto val_tk = m::value_type_to_type_kind(member.value_type);
        out << fmt::format("| `{}` | `(FormID, {})` | {} |\n",
            f.relation_name, type_kind_name(val_tk),
            applicable_types(f.component_idx));
    }

    // Form array relations
    for (size_t i = 0; i < m::kFormArrayCount; i++) {
        auto& fa = m::kFormArrays[i];
        if (!fa.relation_name) continue;
        const char* val_type = "FormID";
        if (fa.field_id == FieldId::Keywords) val_type = "KeywordID";
        else if (fa.field_id == FieldId::Spells) val_type = "SpellID";
        else if (fa.field_id == FieldId::Perks) val_type = "PerkID";
        else if (fa.field_id == FieldId::Factions) val_type = "FactionID";

        out << fmt::format("| `{}` | `(FormID, {})` | {} |\n",
            fa.relation_name, val_type,
            applicable_types(fa.component_idx));
    }
    out << "\n---\n\n";

    // ── Effects: Scalar Setters ──
    out << "## Effects\n\n";
    out << "Effects are the actions Mora applies to matching forms.\n\n";

    out << "### Scalar Setters\n\n";
    out << "| Effect | Signature | Applies to |\n";
    out << "|--------|-----------|------------|\n";
    for (size_t i = 0; i < m::kFieldCount; i++) {
        auto& f = m::kFields[i];
        if (!f.set_action) continue;
        auto& member = m::kComponents[f.component_idx].members[f.member_idx];
        auto val_tk = m::value_type_to_type_kind(member.value_type);
        auto form_tk = m::effect_form_type_kind(f.component_idx);
        out << fmt::format("| `{}` | `({}, {})` | {} |\n",
            f.set_action, type_kind_name(form_tk), type_kind_name(val_tk),
            applicable_types(f.component_idx));
    }
    out << "\n";

    // ── Effects: Collection Operations ──
    out << "### Collection Operations\n\n";
    out << "| Effect | Signature | Applies to |\n";
    out << "|--------|-----------|------------|\n";
    for (size_t i = 0; i < m::kFormArrayCount; i++) {
        auto& fa = m::kFormArrays[i];
        auto form_tk = m::effect_form_type_kind(fa.component_idx);
        const char* val_type = "FormID";
        if (fa.field_id == FieldId::Keywords) val_type = "KeywordID";
        else if (fa.field_id == FieldId::Spells) val_type = "SpellID";
        else if (fa.field_id == FieldId::Perks) val_type = "PerkID";
        else if (fa.field_id == FieldId::Factions) val_type = "FactionID";

        if (fa.add_action) {
            out << fmt::format("| `{}` | `({}, {})` | {} |\n",
                fa.add_action, type_kind_name(form_tk), val_type,
                applicable_types(fa.component_idx));
        }
        if (fa.remove_action) {
            out << fmt::format("| `{}` | `({}, {})` | {} |\n",
                fa.remove_action, type_kind_name(form_tk), val_type,
                applicable_types(fa.component_idx));
        }
    }
    out << "\n";

    // ── Effects: Boolean Flags ──
    out << "### Boolean Flags\n\n";
    out << "| Effect | Signature | Applies to |\n";
    out << "|--------|-----------|------------|\n";
    for (size_t i = 0; i < m::kFlagCount; i++) {
        auto& fl = m::kFlags[i];
        if (!fl.set_action) continue;
        auto form_tk = m::effect_form_type_kind(fl.component_idx);
        out << fmt::format("| `{}` | `({}, Int)` | {} |\n",
            fl.set_action, type_kind_name(form_tk),
            applicable_types(fl.component_idx));
    }
    out << "\n---\n\n";

    // ── Operators ──
    out << "## Comparison Operators\n\n";
    out << "| Operator | Meaning |\n";
    out << "|----------|---------|\n";
    out << "| `==` | Equal |\n";
    out << "| `!=` | Not equal |\n";
    out << "| `<` | Less than |\n";
    out << "| `<=` | Less than or equal |\n";
    out << "| `>` | Greater than |\n";
    out << "| `>=` | Greater than or equal |\n";
    out << "\n";

    out << "## Arithmetic Operators\n\n";
    out << "Arithmetic operators require numeric types (Int or Float).\n\n";
    out << "| Operator | Meaning |\n";
    out << "|----------|---------|\n";
    out << "| `+` | Addition |\n";
    out << "| `-` | Subtraction |\n";
    out << "| `*` | Multiplication |\n";
    out << "| `/` | Division |\n";
}

} // namespace mora
