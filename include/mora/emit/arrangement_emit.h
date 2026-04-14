#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace mora::emit {

std::vector<uint8_t> build_u32_arrangement(
    uint32_t relation_id,
    std::vector<std::array<uint32_t, 2>> rows,
    uint8_t key_column);

std::vector<uint8_t> build_arrangements_section(
    const std::vector<std::vector<uint8_t>>& arrangements);

} // namespace mora::emit
