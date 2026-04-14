#pragma once
#include <cstdint>

namespace mora::model {

enum class HandlerId : uint16_t {
    None = 0,
    // Handler ids are added as relations requiring them are added.
    // Each id must correspond to an entry in kHandlers (handlers.h).
};

} // namespace mora::model
