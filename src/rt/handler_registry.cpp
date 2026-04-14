#include "mora/rt/handler_registry.h"

namespace mora::rt {

void HandlerRegistry::bind_effect(model::HandlerId id, EffectFn fn)   { effects_[static_cast<uint16_t>(id)]  = std::move(fn); }
void HandlerRegistry::bind_retract(model::HandlerId id, RetractFn fn) { retracts_[static_cast<uint16_t>(id)] = std::move(fn); }
void HandlerRegistry::bind_read(model::HandlerId id, ReadFn fn)       { reads_[static_cast<uint16_t>(id)]    = std::move(fn); }

bool HandlerRegistry::has_impl(model::HandlerId id) const {
    auto k = static_cast<uint16_t>(id);
    return effects_.count(k) || retracts_.count(k) || reads_.count(k);
}

EffectHandle HandlerRegistry::invoke_effect(model::HandlerId id, const EffectArgs& a) {
    auto it = effects_.find(static_cast<uint16_t>(id));
    if (it == effects_.end()) return {};
    return it->second(a);
}
void HandlerRegistry::invoke_retract(model::HandlerId id, EffectHandle h) {
    auto it = retracts_.find(static_cast<uint16_t>(id));
    if (it != retracts_.end()) it->second(h);
}
std::vector<uint32_t> HandlerRegistry::invoke_read(model::HandlerId id, const EffectArgs& a) {
    auto it = reads_.find(static_cast<uint16_t>(id));
    if (it == reads_.end()) return {};
    return it->second(a);
}

} // namespace mora::rt
