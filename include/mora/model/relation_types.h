#pragma once
#include <cstdint>
#include <string_view>
#include <array>

namespace mora::model {

// Element types (the atomic types that can appear inside a type constructor)
enum class ElemType : uint8_t {
    Int,
    Float,
    String,
    FormRef,    // generic form pointer
    Keyword,    // interned user symbol
    RefId,      // placed-reference handle (distinct from FormRef)
};

// Type constructors — the shape of a relation's value slot
enum class TypeCtor : uint8_t {
    Scalar,      // one settable value (strings, names, non-countable scalars)
    Countable,   // numeric; supports set/add/sub (damage, gold, multipliers)
    List,        // collection; supports add/remove (keywords, factions, spells)
    Const,       // read-only, no writes permitted
    Predicate,   // unary existence — no value slot (form/npc(F), form/weapon(F))
};

struct TypeExpr {
    TypeCtor ctor = TypeCtor::Scalar;
    ElemType elem = ElemType::Int;  // Ignored for Predicate
};

constexpr TypeExpr scalar(ElemType e)    { return {TypeCtor::Scalar,    e}; }
constexpr TypeExpr countable(ElemType e) { return {TypeCtor::Countable, e}; }
constexpr TypeExpr list_of(ElemType e)   { return {TypeCtor::List,      e}; }
constexpr TypeExpr const_(ElemType e)    { return {TypeCtor::Const,     e}; }
constexpr TypeExpr predicate()           { return {TypeCtor::Predicate, ElemType::Int}; }

constexpr bool operator==(TypeExpr a, TypeExpr b) {
    return a.ctor == b.ctor && a.elem == b.elem;
}

enum class RelationSourceKind : uint8_t {
    Static, MemoryRead, Hook, Handler, Event,
};

struct ArgSpec {
    ElemType        type = ElemType::Int;
    std::string_view name = {};
};

// How to pull a value out of an ESP subrecord. Mirrors the reader-side
// ReadType; kept here to keep model/* decoupled from esp/schema_registry.h.
enum class EspReadType : uint8_t {
    Unspecified,
    Int8, Int16, Int32,
    UInt8, UInt16, UInt32,
    Float32,
    FormID,
    ZString,
    LString,
};

// Extraction kind declared in YAML. Unspecified means "form_model-driven
// registration already covers this relation; don't add a YAML-sourced schema."
enum class EspExtract : uint8_t {
    Unspecified,
    Existence,    // record exists → one unary predicate fact
    Subrecord,    // whole subrecord is one value
    PackedField,  // read at byte offset inside a subrecord
    ArrayField,   // subrecord is an array of fixed-size elements
    ListField,    // repeating subrecords, each one is an element
    BitTest,      // predicate: emit fact if a specific bit is set in a flags word
};

struct EspSource {
    std::string_view record_type = {};
    std::string_view subrecord   = {};
    EspExtract       extract      = EspExtract::Unspecified;
    uint16_t         offset       = 0;   // byte offset within the subrecord
    uint16_t         element_size = 0;   // for ArrayField / ListField
    uint8_t          bit          = 0;   // for BitTest, 0-indexed
    EspReadType      read_as      = EspReadType::Unspecified;
};

struct MemoryReadSpec {
    uint32_t  offset     = 0;
    ElemType  value_type = ElemType::Int;
};

struct HookSpec {
    std::string_view hook_name = {};
    enum class Kind : uint8_t { Edge, State } kind = Kind::Edge;
};

// ── Verb catalog driven by type constructor ──────────────────────────────

enum class VerbKind : uint8_t { Set, Add, Sub, Remove };

struct CtorSpec {
    TypeCtor ctor;
    std::string_view name;
    std::array<VerbKind, 3> verbs;
    uint8_t verb_count;
    bool writable;
};

inline constexpr CtorSpec kCtorTable[] = {
    { TypeCtor::Scalar,    "scalar",    { VerbKind::Set, VerbKind::Set, VerbKind::Set },                 1, true  },
    { TypeCtor::Countable, "countable", { VerbKind::Set, VerbKind::Add, VerbKind::Sub },                 3, true  },
    { TypeCtor::List,      "list",      { VerbKind::Add, VerbKind::Remove, VerbKind::Add },              2, true  },
    { TypeCtor::Const,     "const",     { VerbKind::Set, VerbKind::Set, VerbKind::Set },                 0, false },
    { TypeCtor::Predicate, "predicate", { VerbKind::Set, VerbKind::Set, VerbKind::Set },                 0, false },
};

constexpr const CtorSpec& ctor_spec(TypeCtor c) {
    for (const auto& s : kCtorTable) if (s.ctor == c) return s;
    return kCtorTable[0];
}

constexpr bool is_legal_verb(VerbKind v, TypeExpr t) {
    const auto& s = ctor_spec(t.ctor);
    for (uint8_t i = 0; i < s.verb_count; ++i)
        if (s.verbs[i] == v) return true;
    return false;
}

} // namespace mora::model
