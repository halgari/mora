#include "mora/data/schema_registry.h"
#include "mora/data/action_names.h"
#include "mora/data/form_model.h"
#include "mora/eval/fact_db.h"

namespace mora {

SchemaRegistry::SchemaRegistry(StringPool& pool) : pool_(pool) {}

void SchemaRegistry::register_schema(RelationSchema schema) {
    uint32_t key = schema.name.index;
    schemas_.insert_or_assign(key, std::move(schema));
}

const RelationSchema* SchemaRegistry::lookup(StringId name) const {
    auto it = schemas_.find(name.index);
    if (it == schemas_.end()) return nullptr;
    return &it->second;
}

std::vector<const RelationSchema*> SchemaRegistry::all_schemas() const {
    std::vector<const RelationSchema*> result;
    result.reserve(schemas_.size());
    for (auto& [_, schema] : schemas_) {
        result.push_back(&schema);
    }
    return result;
}

void SchemaRegistry::configure_fact_db(FactDB& db) const {
    for (auto& [_, schema] : schemas_) {
        db.configure_relation(schema.name, schema.column_types.size(),
                              schema.indexed_columns);
    }
}

std::vector<const RelationSchema*> SchemaRegistry::schemas_for_record(
    const std::string& record_type) const {
    std::vector<const RelationSchema*> result;
    for (auto& [_, schema] : schemas_) {
        for (auto& src : schema.esp_sources) {
            if (src.record_type == record_type) {
                result.push_back(&schema);
                break;
            }
        }
    }
    return result;
}

namespace {

ReadType value_type_to_read_type(mora::model::ValueType vt) {
    using VT = mora::model::ValueType;
    switch (vt) {
        case VT::Int8:          return ReadType::Int8;
        case VT::Int16:         return ReadType::Int16;
        case VT::Int32:         return ReadType::Int32;
        case VT::UInt8:         return ReadType::UInt8;
        case VT::UInt16:        return ReadType::UInt16;
        case VT::UInt32:        return ReadType::UInt32;
        case VT::Float32:       return ReadType::Float32;
        case VT::FormRef:       return ReadType::FormID;
        case VT::BSFixedString: return ReadType::LString;
    }
    return ReadType::Int32;
}

MoraType value_type_to_mora_type(mora::model::ValueType vt) {
    using VT = mora::model::ValueType;
    switch (vt) {
        case VT::Float32:       return MoraType::make(TypeKind::Float);
        case VT::BSFixedString: return MoraType::make(TypeKind::String);
        case VT::FormRef:       return MoraType::make(TypeKind::FormID);
        default:                return MoraType::make(TypeKind::Int);
    }
}

} // anonymous namespace

void SchemaRegistry::register_defaults() {
    namespace m = model;
    auto id = [&](const char* s) { return pool_.intern(s); };
    auto formid_type = MoraType::make(TypeKind::FormID);

    // ── Existence relations from modifiable form types ──────────────────
    for (size_t i = 0; i < m::kFormTypeCount; i++) {
        auto& ft = *m::kFormTypes[i];
        RelationSchema s;
        s.name = id(ft.relation_name);
        s.column_types = {MoraType::make(ft.type_kind)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            ft.record_tag, "", EspSource::Kind::Existence, 0, 0, ReadType::FormID});
        register_schema(std::move(s));
    }

    // ── Existence-only form types (no modifiable components) ────────────
    for (size_t i = 0; i < m::kExistenceOnlyCount; i++) {
        auto& eo = m::kExistenceOnly[i];
        RelationSchema s;
        s.name = id(eo.relation_name);
        s.column_types = {MoraType::make(eo.type_kind)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            eo.record_tag, "", EspSource::Kind::Existence, 0, 0, ReadType::FormID});
        register_schema(std::move(s));
    }

    // ── Scalar field relations from ESP sources ────────────────────────
    for (size_t fi = 0; fi < m::kEspSourceCount; fi++) {
        auto& esp = m::kEspSources[fi];
        auto& field = m::kFields[esp.field_idx];
        if (!field.relation_name) continue;

        auto* existing = lookup(id(field.relation_name));
        if (existing) {
            // Already registered — add this ESP source to the existing schema.
            // We need to re-register with the additional source.
            RelationSchema s = *existing;
            s.esp_sources.push_back(EspSource{
                esp.record_tag, esp.subrecord_tag,
                EspSource::Kind::PackedField, esp.esp_offset, 0,
                value_type_to_read_type(esp.read_type)});
            register_schema(std::move(s));
        } else {
            RelationSchema s;
            s.name = id(field.relation_name);
            auto& member = m::kComponents[field.component_idx].members[field.member_idx];
            s.column_types = {formid_type, value_type_to_mora_type(member.value_type)};
            s.indexed_columns = {0};
            s.esp_sources.push_back(EspSource{
                esp.record_tag, esp.subrecord_tag,
                EspSource::Kind::PackedField, esp.esp_offset, 0,
                value_type_to_read_type(esp.read_type)});
            register_schema(std::move(s));
        }
    }

    // ── Form array relations (has_keyword, has_spell, etc.) ────────────
    for (size_t i = 0; i < m::kFormArrayCount; i++) {
        auto& fa = m::kFormArrays[i];
        if (!fa.relation_name || !fa.subrecord_tag) continue;

        RelationSchema s;
        s.name = id(fa.relation_name);
        s.column_types = {formid_type, MoraType::make(TypeKind::FormID)};

        // Set column type for the value based on field ID
        if (fa.field_id == FieldId::Keywords)
            s.column_types[1] = MoraType::make(TypeKind::KeywordID);
        else if (fa.field_id == FieldId::Spells)
            s.column_types[1] = MoraType::make(TypeKind::SpellID);
        else if (fa.field_id == FieldId::Perks)
            s.column_types[1] = MoraType::make(TypeKind::PerkID);
        else if (fa.field_id == FieldId::Factions)
            s.column_types[1] = MoraType::make(TypeKind::FactionID);

        s.indexed_columns = {0, 1};

        auto esp_kind = (fa.esp_kind == m::FormArrayDef::EspKind::ArrayField)
            ? EspSource::Kind::ArrayField : EspSource::Kind::ListField;

        // Add ESP sources for all form types that have this component
        if (fa.field_id == FieldId::Keywords) {
            for (size_t j = 0; j < m::kKeywordRecordCount; j++) {
                s.esp_sources.push_back(EspSource{
                    m::kKeywordRecords[j], fa.subrecord_tag,
                    esp_kind, 0, fa.element_size, ReadType::FormID});
            }
        } else {
            // For spells, perks, factions — NPC only
            s.esp_sources.push_back(EspSource{
                "NPC_", fa.subrecord_tag, esp_kind, 0, fa.element_size, ReadType::FormID});
        }
        register_schema(std::move(s));
    }

    // ── Special relations not in the model ──────────────────────────────
    // editor_id, name (FULL), npc_flags, race_of, npc_gender, form_source

    // editor_id(FormID, String) — EDID subrecord from many record types
    {
        RelationSchema s;
        s.name = id("editor_id");
        s.column_types = {formid_type, MoraType::make(TypeKind::String)};
        s.indexed_columns = {0};
        for (size_t i = 0; i < m::kEditorIdRecordCount; i++) {
            s.esp_sources.push_back(EspSource{
                m::kEditorIdRecords[i], "EDID",
                EspSource::Kind::Subrecord, 0, 0, ReadType::ZString});
        }
        register_schema(std::move(s));
    }

    // name(FormID, String) — FULL subrecord (LString)
    {
        RelationSchema s;
        s.name = id("name");
        s.column_types = {formid_type, MoraType::make(TypeKind::String)};
        s.indexed_columns = {0};
        for (size_t i = 0; i < m::kFullNameRecordCount; i++) {
            s.esp_sources.push_back(EspSource{
                m::kFullNameRecords[i], "FULL",
                EspSource::Kind::Subrecord, 0, 0, ReadType::LString});
        }
        register_schema(std::move(s));
    }

    // npc_flags(FormID, Int) — ACBS flags at offset 0
    {
        RelationSchema s;
        s.name = id("npc_flags");
        s.column_types = {formid_type, MoraType::make(TypeKind::Int)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "NPC_", "ACBS", EspSource::Kind::PackedField, 0, 0, ReadType::UInt32});
        register_schema(std::move(s));
    }

    // race_of(FormID, RaceID) — RNAM subrecord
    {
        RelationSchema s;
        s.name = id("race_of");
        s.column_types = {formid_type, MoraType::make(TypeKind::RaceID)};
        s.indexed_columns = {0, 1};
        s.esp_sources.push_back(EspSource{
            "NPC_", "RNAM", EspSource::Kind::Subrecord, 0, 0, ReadType::FormID});
        register_schema(std::move(s));
    }
}

} // namespace mora
