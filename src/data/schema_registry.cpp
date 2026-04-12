#include "mora/data/schema_registry.h"
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

void SchemaRegistry::register_defaults() {
    // Helper lambdas
    auto id = [&](const char* s) { return pool_.intern(s); };
    auto formid_type = MoraType::make(TypeKind::FormID);

    // --- Existence relations ---
    struct ExistenceDef {
        const char* name;
        TypeKind type;
        const char* record;
    };
    ExistenceDef existence_defs[] = {
        {"npc",          TypeKind::NpcID,     "NPC_"},
        {"weapon",       TypeKind::WeaponID,  "WEAP"},
        {"armor",        TypeKind::ArmorID,   "ARMO"},
        {"spell",        TypeKind::SpellID,   "SPEL"},
        {"perk",         TypeKind::PerkID,    "PERK"},
        {"keyword",      TypeKind::KeywordID, "KYWD"},
        {"faction",      TypeKind::FactionID, "FACT"},
        {"race",         TypeKind::RaceID,    "RACE"},
        {"leveled_list", TypeKind::FormID,    "LVLI"},
    };
    for (auto& def : existence_defs) {
        RelationSchema s;
        s.name = id(def.name);
        s.column_types = {MoraType::make(def.type)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            def.record, "", EspSource::Kind::Existence, 0, 0, ReadType::FormID});
        register_schema(std::move(s));
    }

    // --- has_keyword(FormID, KeywordID) - ArrayField from KWDA ---
    {
        RelationSchema s;
        s.name = id("has_keyword");
        s.column_types = {formid_type, MoraType::make(TypeKind::KeywordID)};
        s.indexed_columns = {0, 1};
        const char* kw_records[] = {"NPC_", "WEAP", "ARMO", "ALCH", "BOOK", "AMMO", "CONT", "MGEF"};
        for (auto* rec : kw_records) {
            s.esp_sources.push_back(EspSource{
                rec, "KWDA", EspSource::Kind::ArrayField, 0, 4, ReadType::FormID});
        }
        register_schema(std::move(s));
    }

    // --- has_faction(FormID, FactionID) - ListField from SNAM ---
    {
        RelationSchema s;
        s.name = id("has_faction");
        s.column_types = {formid_type, MoraType::make(TypeKind::FactionID)};
        s.indexed_columns = {0, 1};
        s.esp_sources.push_back(EspSource{
            "NPC_", "SNAM", EspSource::Kind::ListField, 0, 8, ReadType::FormID});
        register_schema(std::move(s));
    }

    // --- has_spell(FormID, SpellID) - ListField from SPLO ---
    {
        RelationSchema s;
        s.name = id("has_spell");
        s.column_types = {formid_type, MoraType::make(TypeKind::SpellID)};
        s.indexed_columns = {0, 1};
        s.esp_sources.push_back(EspSource{
            "NPC_", "SPLO", EspSource::Kind::ListField, 0, 4, ReadType::FormID});
        register_schema(std::move(s));
    }

    // --- has_perk(FormID, PerkID) - ListField from PRKR ---
    {
        RelationSchema s;
        s.name = id("has_perk");
        s.column_types = {formid_type, MoraType::make(TypeKind::PerkID)};
        s.indexed_columns = {0, 1};
        s.esp_sources.push_back(EspSource{
            "NPC_", "PRKR", EspSource::Kind::ListField, 0, 8, ReadType::FormID});
        register_schema(std::move(s));
    }

    // --- editor_id(FormID, String) - Subrecord EDID ZString from many types ---
    {
        RelationSchema s;
        s.name = id("editor_id");
        s.column_types = {formid_type, MoraType::make(TypeKind::String)};
        s.indexed_columns = {0};
        const char* edid_records[] = {
            "NPC_", "WEAP", "ARMO", "SPEL", "PERK", "KYWD", "FACT", "RACE",
            "LVLI", "ALCH", "BOOK", "AMMO", "CONT", "MGEF"};
        for (auto* rec : edid_records) {
            s.esp_sources.push_back(EspSource{
                rec, "EDID", EspSource::Kind::Subrecord, 0, 0, ReadType::ZString});
        }
        register_schema(std::move(s));
    }

    // --- name(FormID, String) - Subrecord FULL LString ---
    {
        RelationSchema s;
        s.name = id("name");
        s.column_types = {formid_type, MoraType::make(TypeKind::String)};
        s.indexed_columns = {0};
        const char* name_records[] = {"NPC_", "WEAP", "ARMO", "ALCH", "BOOK"};
        for (auto* rec : name_records) {
            s.esp_sources.push_back(EspSource{
                rec, "FULL", EspSource::Kind::Subrecord, 0, 0, ReadType::LString});
        }
        register_schema(std::move(s));
    }

    // --- damage(FormID, Int) - PackedField DATA offset 8 Int16 from WEAP ---
    {
        RelationSchema s;
        s.name = id("damage");
        s.column_types = {formid_type, MoraType::make(TypeKind::Int)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "WEAP", "DATA", EspSource::Kind::PackedField, 8, 0, ReadType::Int16});
        register_schema(std::move(s));
    }

    // --- gold_value(FormID, Int) - PackedField DATA offset 0 Int32 from WEAP ---
    {
        RelationSchema s;
        s.name = id("gold_value");
        s.column_types = {formid_type, MoraType::make(TypeKind::Int)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "WEAP", "DATA", EspSource::Kind::PackedField, 0, 0, ReadType::Int32});
        register_schema(std::move(s));
    }

    // --- weight(FormID, Float) - PackedField DATA offset 4 Float32 from WEAP ---
    {
        RelationSchema s;
        s.name = id("weight");
        s.column_types = {formid_type, MoraType::make(TypeKind::Float)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "WEAP", "DATA", EspSource::Kind::PackedField, 4, 0, ReadType::Float32});
        register_schema(std::move(s));
    }

    // --- armor_rating(FormID, Int) - Subrecord DNAM offset 0 Float32 from ARMO ---
    {
        RelationSchema s;
        s.name = id("armor_rating");
        s.column_types = {formid_type, MoraType::make(TypeKind::Int)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "ARMO", "DNAM", EspSource::Kind::PackedField, 0, 0, ReadType::Float32});
        register_schema(std::move(s));
    }

    // --- base_level(FormID, Int) - PackedField ACBS offset 8 Int16 from NPC_ ---
    {
        RelationSchema s;
        s.name = id("base_level");
        s.column_types = {formid_type, MoraType::make(TypeKind::Int)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "NPC_", "ACBS", EspSource::Kind::PackedField, 8, 0, ReadType::Int16});
        register_schema(std::move(s));
    }

    // --- race_of(FormID, RaceID) - Subrecord RNAM FormID from NPC_ ---
    {
        RelationSchema s;
        s.name = id("race_of");
        s.column_types = {formid_type, MoraType::make(TypeKind::RaceID)};
        s.indexed_columns = {0};
        s.esp_sources.push_back(EspSource{
            "NPC_", "RNAM", EspSource::Kind::Subrecord, 0, 0, ReadType::FormID});
        register_schema(std::move(s));
    }
}

} // namespace mora
