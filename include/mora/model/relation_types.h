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

struct EspSource {
    std::string_view record_type = {};
    std::string_view subrecord  = {};
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
