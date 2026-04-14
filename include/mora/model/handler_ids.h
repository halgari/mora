#pragma once
#include <cstdint>

namespace mora::model {

enum class HandlerId : uint16_t {
    None = 0,
    // ref/* effect handlers
    RefAddKeyword          = 1,
    RefRemoveKeyword       = 2,
    // ref/* relation readers
    RefReadCurrentLocation = 10,
    RefReadInCombat        = 11,
    RefReadBaseForm        = 12,
    // player/* effect handlers
    PlayerAddGold          = 20,
    PlayerSubGold          = 21,
    PlayerShowNotification = 22,

    // ref/* type-predicate read handlers
    RefReadIsPlayer        = 40,
    RefReadIsNpc           = 41,
    RefReadIsWeapon        = 42,
    RefReadIsArmor         = 43,
    RefReadIsDead          = 44,

    // ref/* actor-state read handlers
    RefReadHealth          = 50,
    RefReadMagicka         = 51,
    RefReadStamina         = 52,
    RefReadLevel           = 53,

    // ref/* equipment read handlers
    RefReadEquippedWeapon      = 60,
    RefReadEquippedWeaponLeft  = 61,
    RefReadEquippedSpellLeft   = 62,
    RefReadEquippedSpellRight  = 63,
    RefReadEquippedArmor       = 64,
    RefReadInventoryItem       = 65,

    // ref/* item-state read handlers
    RefReadWornBy              = 70,
    RefReadContainer           = 71,
    RefReadIsEquipped          = 72,
    RefReadIsStolen            = 73,
};

} // namespace mora::model
