#include "mora/core/digest.h"
#include "sha256.h"

namespace mora {

std::array<uint8_t, 32> compute_digest(std::string_view data) {
    std::array<uint8_t, 32> out{};
    sha256::Ctx ctx;
    sha256::init(ctx);
    sha256::update(ctx, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    sha256::finish(ctx, out.data());
    return out;
}

} // namespace mora
