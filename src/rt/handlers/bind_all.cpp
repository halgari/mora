#include "mora/rt/handler_impls.h"

namespace mora::rt {

#ifdef _WIN32
void bind_ref_handlers(HandlerRegistry&);
void bind_player_handlers(HandlerRegistry&);
#endif

void bind_all_handlers(HandlerRegistry& reg) {
#ifdef _WIN32
    bind_ref_handlers(reg);
    bind_player_handlers(reg);
#else
    (void)reg;  // Linux build has no real handlers; engine still runs via test stubs.
#endif
}

} // namespace mora::rt
