#pragma once
#include "mora/model/handler_ids.h"
#include <string_view>

namespace mora::model {

struct HandlerEntry {
    HandlerId        id;
    std::string_view name;
    bool             generic;   // true if no dedicated C++ fn is required
};

inline constexpr HandlerEntry kHandlers[] = {
    { HandlerId::None, "none", true },
};

constexpr const HandlerEntry* find_handler(HandlerId id) {
    for (const auto& h : kHandlers) if (h.id == id) return &h;
    return nullptr;
}

} // namespace mora::model
