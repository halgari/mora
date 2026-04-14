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
    { HandlerId::None,                   "none",                      true  },
    { HandlerId::RefAddKeyword,          "ref_add_keyword",           false },
    { HandlerId::RefRemoveKeyword,       "ref_remove_keyword",        false },
    { HandlerId::RefReadCurrentLocation, "ref_read_current_location", false },
    { HandlerId::RefReadInCombat,        "ref_read_in_combat",        false },
    { HandlerId::RefReadBaseForm,        "ref_read_base_form",        false },
    { HandlerId::PlayerAddGold,          "player_add_gold",           false },
    { HandlerId::PlayerSubGold,          "player_sub_gold",           false },
    { HandlerId::PlayerShowNotification, "player_show_notification",  false },
};

constexpr const HandlerEntry* find_handler(HandlerId id) {
    for (const auto& h : kHandlers) if (h.id == id) return &h;
    return nullptr;
}

} // namespace mora::model
