#include "mora/data/schema_registry.h"
#include "mora/data/action_names.h"
#include "mora/data/form_model.h"
#include "mora/eval/fact_db.h"
#include "mora/model/relations.h"

namespace mora {

SchemaRegistry::SchemaRegistry(StringPool& pool) : pool_(pool) {}

void SchemaRegistry::register_schema(RelationSchema schema) {
    uint32_t const key = schema.name.index;
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

const Type* value_type_to_type_ptr(mora::model::ValueType vt) {
    using VT = mora::model::ValueType;
    switch (vt) {
        case VT::Float32:       return mora::types::get("Float64");
        case VT::BSFixedString: return mora::types::get("String");
        case VT::FormRef:       return mora::types::get("FormID");
        default:                return mora::types::get("Int64");
    }
}

} // anonymous namespace

void SchemaRegistry::register_defaults() {
    namespace m = model;
    auto id = [&](const char* s) { return pool_.intern(s); };
    auto formid_type = mora::types::get("FormID");

    // ── Existence relations from modifiable form types ──────────────────
    for (size_t i = 0; i < m::kFormTypeCount; i++) {
        auto& ft = *m::kFormTypes[i];
        RelationSchema s;
        s.name = id(ft.relation_name);
        s.column_types = {mora::types::get(ft.type_name)};
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
        s.column_types = {mora::types::get(eo.type_name)};
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
            s.column_types = {formid_type, value_type_to_type_ptr(member.value_type)};
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
        s.column_types = {formid_type, mora::types::get("FormID")};

        // Set column type for the value based on field ID
        if (fa.field_id == FieldId::Keywords)
            s.column_types[1] = mora::types::get("KeywordID");
        else if (fa.field_id == FieldId::Spells)
            s.column_types[1] = mora::types::get("SpellID");
        else if (fa.field_id == FieldId::Perks)
            s.column_types[1] = mora::types::get("PerkID");
        else if (fa.field_id == FieldId::Factions)
            s.column_types[1] = mora::types::get("FactionID");

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
        s.column_types = {formid_type, mora::types::get("String")};
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
        s.column_types = {formid_type, mora::types::get("String")};
        s.indexed_columns = {0};
        for (size_t i = 0; i < m::kFullNameRecordCount; i++) {
            s.esp_sources.push_back(EspSource{
                m::kFullNameRecords[i], "FULL",
                EspSource::Kind::Subrecord, 0, 0, ReadType::LString});
        }
        register_schema(std::move(s));
    }

    // npc_flags(FormID, Int64) — ACBS flags at offset 0
    {
        RelationSchema s;
        s.name = id("npc_flags");
        s.column_types = {formid_type, mora::types::get("Int64")};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "NPC_", "ACBS", EspSource::Kind::PackedField, 0, 0, ReadType::UInt32});
        register_schema(std::move(s));
    }

    // race_of(FormID, RaceID) — RNAM subrecord
    {
        RelationSchema s;
        s.name = id("race_of");
        s.column_types = {formid_type, mora::types::get("RaceID")};
        s.indexed_columns = {0, 1};
        s.esp_sources.push_back(EspSource{
            "NPC_", "RNAM", EspSource::Kind::Subrecord, 0, 0, ReadType::FormID});
        register_schema(std::move(s));
    }

    // ── YAML-sourced relations (data/relations/*.yaml) ─────────────────
    // For every kRelations entry that carries extraction metadata (esp.extract
    // != Unspecified) AND isn't already registered via form_model.h above,
    // build a RelationSchema from the YAML declaration.
    register_yaml_relations();

    // ── Plugin-level relations (populated from LoadOrder, not from ESP
    //    scanning). Schemas declared here so rules can type-check
    //    against them; rows are pushed by populate_plugin_facts().
    auto string_type = mora::types::get("String");
    auto int_type    = mora::types::get("Int64");
    auto float_type  = mora::types::get("Float64");
    auto reg_unary = [&](const char* n) {
        RelationSchema s;
        s.name = id(n);
        s.column_types   = {string_type};
        s.indexed_columns = {0};
        register_schema(std::move(s));
    };
    auto reg_pair = [&](const char* n, const Type* second) {
        RelationSchema s;
        s.name = id(n);
        s.column_types   = {string_type, second};
        s.indexed_columns = {0};
        register_schema(std::move(s));
    };
    reg_unary("plugin_exists");
    reg_unary("plugin_is_master");
    reg_unary("plugin_is_light");
    reg_pair ("plugin_load_index", int_type);
    reg_pair ("plugin_version",    float_type);
    reg_pair ("plugin_extension",  string_type);
    reg_pair ("plugin_master_of",  string_type);
}

namespace {

const Type* elem_to_type_ptr(mora::model::ElemType e) {
    using E = mora::model::ElemType;
    switch (e) {
        case E::Int:     return mora::types::get("Int64");
        case E::Float:   return mora::types::get("Float64");
        case E::String:  return mora::types::get("String");
        case E::FormRef: return mora::types::get("FormID");
        case E::Keyword: return mora::types::get("KeywordID");
        case E::RefId:   return mora::types::get("FormID");
    }
    return mora::types::get("Int64");
}

ReadType esp_read_to_read_type(mora::model::EspReadType r) {
    using R = mora::model::EspReadType;
    switch (r) {
        case R::Int8:    return ReadType::Int8;
        case R::Int16:   return ReadType::Int16;
        case R::Int32:   return ReadType::Int32;
        case R::UInt8:   return ReadType::UInt8;
        case R::UInt16:  return ReadType::UInt16;
        case R::UInt32:  return ReadType::UInt32;
        case R::Float32: return ReadType::Float32;
        case R::FormID:  return ReadType::FormID;
        case R::ZString: return ReadType::ZString;
        case R::LString: return ReadType::LString;
        case R::Unspecified: break;
    }
    return ReadType::Int32;
}

EspSource::Kind esp_extract_to_kind(mora::model::EspExtract e) {
    using E = mora::model::EspExtract;
    switch (e) {
        case E::Existence:   return EspSource::Kind::Existence;
        case E::Subrecord:   return EspSource::Kind::Subrecord;
        case E::PackedField: return EspSource::Kind::PackedField;
        case E::ArrayField:  return EspSource::Kind::ArrayField;
        case E::ListField:   return EspSource::Kind::ListField;
        case E::BitTest:     return EspSource::Kind::BitTest;
        case E::Unspecified: break;
    }
    return EspSource::Kind::Existence;
}

} // anonymous

void SchemaRegistry::register_yaml_relations() {
    namespace mm = mora::model;
    for (size_t i = 0; i < mm::kRelationCount; ++i) {
        const auto& r = mm::kRelations[i];
        if (r.source != mm::RelationSourceKind::Static) continue;
        if (r.esp_source.extract == mm::EspExtract::Unspecified) continue;

        auto name_id = pool_.intern(std::string(r.namespace_) + "_" + std::string(r.name));
        // Legacy relation naming didn't use the namespace prefix; try bare name
        // first and only fall back to namespaced if there's a collision.
        name_id = pool_.intern(std::string(r.name));
        if (lookup(name_id)) continue; // form_model.h already covers it

        RelationSchema s;
        s.name = name_id;
        // Column types from the declared args.
        for (uint8_t c = 0; c < r.arg_count; ++c) {
            s.column_types.push_back(elem_to_type_ptr(r.args[c].type));
        }
        // Predicates are unary; everything else has a FormID-keyed primary.
        if (r.arg_count > 0) s.indexed_columns.push_back(0);

        EspSource src;
        src.record_type  = std::string(r.esp_source.record_type);
        src.subrecord_tag = std::string(r.esp_source.subrecord);
        src.kind         = esp_extract_to_kind(r.esp_source.extract);
        src.offset       = r.esp_source.offset;
        src.element_size = r.esp_source.element_size;
        src.read_type    = esp_read_to_read_type(r.esp_source.read_as);
        src.bit          = r.esp_source.bit;
        s.esp_sources.push_back(std::move(src));
        register_schema(std::move(s));
    }
}

} // namespace mora
