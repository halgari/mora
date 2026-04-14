#pragma once
#include "mora/model/relations.h"
#include "mora/model/handlers.h"

namespace mora::model {

enum class VerbKind : uint8_t { Set, Add, Sub, Remove };

constexpr bool is_legal_verb_for(VerbKind v, Cardinality c) {
    switch (c) {
        case Cardinality::Scalar:     return v == VerbKind::Set;
        case Cardinality::Countable:  return v == VerbKind::Set || v == VerbKind::Add || v == VerbKind::Sub;
        case Cardinality::Set:        return v == VerbKind::Add || v == VerbKind::Remove;
        case Cardinality::Functional: return false;
    }
    return false;
}

constexpr bool has_duplicate(const RelationEntry* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            if (arr[i].namespace_ == arr[j].namespace_ && arr[i].name == arr[j].name) return true;
        }
    }
    return false;
}

constexpr bool handler_registered(HandlerId id) {
    if (id == HandlerId::None) return true;
    for (const auto& h : kHandlers) if (h.id == id) return true;
    return false;
}

constexpr bool validate_all_handlers(const RelationEntry* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!handler_registered(arr[i].apply_handler))   return false;
        if (!handler_registered(arr[i].retract_handler)) return false;
    }
    return true;
}

constexpr bool validate_verb_shapes(const RelationEntry* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (arr[i].cardinality == Cardinality::Functional) {
            if (arr[i].apply_handler != HandlerId::None || arr[i].retract_handler != HandlerId::None)
                return false;
        }
    }
    return true;
}

constexpr bool validate_all(const RelationEntry* arr, size_t n) {
    return !has_duplicate(arr, n)
        && validate_all_handlers(arr, n)
        && validate_verb_shapes(arr, n);
}

} // namespace mora::model
