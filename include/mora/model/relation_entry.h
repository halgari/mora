#pragma once
#include "mora/model/relation_types.h"
#include "mora/model/handler_ids.h"
#include <cstdint>
#include <string_view>

namespace mora::model {

inline constexpr uint8_t kMaxArgs = 4;

struct RelationEntry {
    std::string_view   namespace_;
    std::string_view   name;
    ArgSpec            args[kMaxArgs]{};
    uint8_t            arg_count = 0;

    TypeExpr           type;

    RelationSourceKind source = RelationSourceKind::Static;

    // Exactly one of these is populated based on `source`.
    EspSource          esp_source  = {};
    MemoryReadSpec     memory_read = {};
    HookSpec           hook        = {};

    HandlerId          apply_handler   = HandlerId::None;
    HandlerId          retract_handler = HandlerId::None;
    HandlerId          read_handler    = HandlerId::None;

    std::string_view   docs = {};
};

} // namespace mora::model
