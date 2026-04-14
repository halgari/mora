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

    { HandlerId::RefReadIsPlayer,        "ref_read_is_player",        false },
    { HandlerId::RefReadIsNpc,           "ref_read_is_npc",           false },
    { HandlerId::RefReadIsWeapon,        "ref_read_is_weapon",        false },
    { HandlerId::RefReadIsArmor,         "ref_read_is_armor",         false },
    { HandlerId::RefReadIsDead,          "ref_read_is_dead",          false },

    { HandlerId::RefReadHealth,          "ref_read_health",           false },
    { HandlerId::RefReadMagicka,         "ref_read_magicka",          false },
    { HandlerId::RefReadStamina,         "ref_read_stamina",          false },
    { HandlerId::RefReadLevel,           "ref_read_level",            false },

    { HandlerId::RefReadEquippedWeapon,     "ref_read_equipped_weapon",      false },
    { HandlerId::RefReadEquippedWeaponLeft, "ref_read_equipped_weapon_left", false },
    { HandlerId::RefReadEquippedSpellLeft,  "ref_read_equipped_spell_left",  false },
    { HandlerId::RefReadEquippedSpellRight, "ref_read_equipped_spell_right", false },
    { HandlerId::RefReadEquippedArmor,      "ref_read_equipped_armor",       false },
    { HandlerId::RefReadInventoryItem,      "ref_read_inventory_item",       false },

    { HandlerId::RefReadWornBy,          "ref_read_worn_by",          false },
    { HandlerId::RefReadContainer,       "ref_read_container",        false },
    { HandlerId::RefReadIsEquipped,      "ref_read_is_equipped",      false },
    { HandlerId::RefReadIsStolen,        "ref_read_is_stolen",        false },
};

constexpr const HandlerEntry* find_handler(HandlerId id) {
    for (const auto& h : kHandlers) if (h.id == id) return &h;
    return nullptr;
}

} // namespace mora::model
