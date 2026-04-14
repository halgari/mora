#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace mora {

std::array<uint8_t, 32> compute_digest(std::string_view data);

} // namespace mora
