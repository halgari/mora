#pragma once
#include <cstdint>
#include <string_view>

namespace mora::model {

enum class RelValueType : uint8_t {
    Int, Float, String, FormRef, Keyword,
};

enum class Cardinality : uint8_t {
    Scalar,      // single value; verb: set
    Countable,   // numeric single value; verbs: set, add, sub
    Set,         // multi-valued membership; verbs: add, remove
    Functional,  // 1:1 mapping, read-only
};

enum class RelationSourceKind : uint8_t {
    Static,       // derived from ESP at compile time
    MemoryRead,   // runtime offset read
    Hook,         // runtime SKSE hook (state relation)
    Handler,      // runtime custom accessor
    Event,        // runtime edge-triggered (only consumed by `on` rules)
};

struct ArgSpec {
    RelValueType  type = RelValueType::Int;
    std::string_view name = {};
};

struct EspSource {
    std::string_view record_type = {};    // e.g. "NPC_", "WEAP"
    std::string_view subrecord  = {};     // e.g. "KWDA", "SNAM"
};

struct MemoryReadSpec {
    uint32_t     offset    = 0;
    RelValueType value_type = RelValueType::Int;
};

struct HookSpec {
    std::string_view hook_name = {};
    enum class Kind : uint8_t { Edge, State } kind = Kind::Edge;
};

} // namespace mora::model
