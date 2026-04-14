#pragma once
#include "mora/model/relation_types.h"
#include <cstdint>
#include <string_view>

namespace mora::model {

// A built-in pure function. Pure = no side effects, deterministic, evaluated
// eagerly at rule evaluation time (both static and dynamic rules use this).
struct BuiltinFn {
    std::string_view name;
    uint8_t arity;                      // exact number of args
    // Result element type hint. Numeric results widen to Float when any arg
    // is Float at runtime; this value is the "integer" default shape.
    ElemType result_hint;
};

inline constexpr BuiltinFn kBuiltins[] = {
    { "max",   2, ElemType::Int },
    { "min",   2, ElemType::Int },
    { "abs",   1, ElemType::Int },
    { "clamp", 3, ElemType::Int },
};

constexpr const BuiltinFn* find_builtin(std::string_view name) {
    for (const auto& b : kBuiltins) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

} // namespace mora::model
