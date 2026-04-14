#pragma once
#include "mora/model/handler_ids.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace mora::rt {

struct EffectHandle { uint64_t id = 0; };
struct EffectArgs { std::vector<uint32_t> args; };

using EffectFn  = std::function<EffectHandle(const EffectArgs&)>;
using RetractFn = std::function<void(EffectHandle)>;
using ReadFn    = std::function<std::vector<uint32_t>(const EffectArgs&)>;

class HandlerRegistry {
public:
    void bind_effect(model::HandlerId id, EffectFn fn);
    void bind_retract(model::HandlerId id, RetractFn fn);
    void bind_read(model::HandlerId id, ReadFn fn);
    bool has_impl(model::HandlerId id) const;
    EffectHandle invoke_effect(model::HandlerId id, const EffectArgs& a);
    void invoke_retract(model::HandlerId id, EffectHandle h);
    std::vector<uint32_t> invoke_read(model::HandlerId id, const EffectArgs& a);
private:
    std::unordered_map<uint16_t, EffectFn>  effects_;
    std::unordered_map<uint16_t, RetractFn> retracts_;
    std::unordered_map<uint16_t, ReadFn>    reads_;
};

} // namespace mora::rt
