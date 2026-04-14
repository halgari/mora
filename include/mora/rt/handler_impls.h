#pragma once
#include "mora/rt/handler_registry.h"

namespace mora::rt {

// Binds concrete CommonLibSSE-NG implementations to all HandlerId entries.
// Only does real work on Windows; the Linux build compiles a stub.
void bind_all_handlers(HandlerRegistry& reg);

} // namespace mora::rt
