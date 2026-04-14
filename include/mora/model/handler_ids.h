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
};

} // namespace mora::model
