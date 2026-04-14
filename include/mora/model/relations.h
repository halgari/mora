#pragma once
#include "mora/model/relation_entry.h"
#include <cstddef>

namespace mora::model {

// Defined in src/model/relations_seed.cpp.
// kRelations is constexpr (implicitly inline) so it can be used in static_assert
// validators. kRelationCount remains extern const to track the logical count
// (sentinel entry excluded).
extern const RelationEntry kRelations[];
extern const size_t        kRelationCount;

constexpr const RelationEntry* find_relation(std::string_view ns, std::string_view name,
                                             const RelationEntry* arr, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (arr[i].namespace_ == ns && arr[i].name == name) return &arr[i];
    }
    return nullptr;
}

} // namespace mora::model
