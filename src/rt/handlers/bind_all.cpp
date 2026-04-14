#include "mora/rt/handler_impls.h"

namespace mora::rt {

#ifdef _WIN32
void bind_ref_handlers(HandlerRegistry&, const std::unordered_set<uint16_t>&);
void bind_player_handlers(HandlerRegistry&, const std::unordered_set<uint16_t>&);
void bind_ref_type_handlers(HandlerRegistry&, const std::unordered_set<uint16_t>&);
void bind_ref_actor_handlers(HandlerRegistry&, const std::unordered_set<uint16_t>&);
void bind_ref_item_handlers(HandlerRegistry&, const std::unordered_set<uint16_t>&);
#endif

void bind_all_handlers(HandlerRegistry& reg,
                       const std::unordered_set<uint16_t>& needed) {
#ifdef _WIN32
    bind_ref_handlers(reg, needed);
    bind_player_handlers(reg, needed);
    bind_ref_type_handlers(reg, needed);
    bind_ref_actor_handlers(reg, needed);
    bind_ref_item_handlers(reg, needed);
#else
    (void)reg; (void)needed;
#endif
}

} // namespace mora::rt
