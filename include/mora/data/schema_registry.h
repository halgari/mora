#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "mora/core/type.h"
#include "mora/core/string_pool.h"

namespace mora {

class FactDB;

// How to read a value from ESP subrecord data
enum class ReadType : uint8_t {
    FormID, Int8, Int16, Int32, UInt8, UInt16, UInt32, Float32, ZString, LString
};

// How a relation is populated from ESP files
struct EspSource {
    std::string record_type;      // "NPC_", "WEAP", etc.
    std::string subrecord_tag;    // "KWDA", "DATA", "" for existence
    enum class Kind {
        Existence,     // record exists -> single FormID fact
        Subrecord,     // single value from a subrecord
        PackedField,   // field at byte offset within a packed subrecord
        ArrayField,    // subrecord contains array of fixed-size elements
        ListField,     // repeating subrecords
        BitTest,       // predicate: bit N is set in a flags word at offset
    } kind = Kind::Existence;
    size_t offset = 0;
    size_t element_size = 0;
    ReadType read_type = ReadType::FormID;
    uint8_t bit = 0;
};

struct RelationSchema {
    StringId name;
    std::vector<const Type*> column_types;   // each points to a Type singleton
    std::vector<size_t> indexed_columns;
    std::vector<EspSource> esp_sources;
};

class SchemaRegistry {
public:
    explicit SchemaRegistry(StringPool& pool);

    void register_defaults();  // all built-in Skyrim relations
    void register_schema(RelationSchema schema);

    const RelationSchema* lookup(StringId name) const;
    std::vector<const RelationSchema*> all_schemas() const;

    // Pre-configure a FactDB with proper arities and indexes
    void configure_fact_db(FactDB& db) const;

    // Get schemas that extract from a given record type
    std::vector<const RelationSchema*> schemas_for_record(const std::string& record_type) const;

    // Number of registered relation schemas.
    size_t relation_count() const { return schemas_.size(); }

private:
    void register_yaml_relations();

    StringPool& pool_;
    std::unordered_map<uint32_t, RelationSchema> schemas_;
};

} // namespace mora
