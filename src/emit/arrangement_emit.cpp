#include "mora/emit/arrangement_emit.h"
#include "mora/emit/arrangement.h"
#include <algorithm>
#include <cstring>

namespace mora::emit {

std::vector<uint8_t> build_u32_arrangement(
    uint32_t relation_id,
    std::vector<std::array<uint32_t, 2>> rows,
    uint8_t key_column) {

    std::sort(rows.begin(), rows.end(),
        [key_column](const auto& a, const auto& b) {
            return a[key_column] < b[key_column];
        });

    ArrangementHeader h{};
    h.relation_id      = relation_id;
    h.row_count        = static_cast<uint32_t>(rows.size());
    h.row_stride_bytes = 8;
    h.key_column_index = key_column;
    h.key_type         = 0;

    std::vector<uint8_t> out(sizeof(h) + rows.size() * 8);
    std::memcpy(out.data(), &h, sizeof(h));
    for (size_t i = 0; i < rows.size(); ++i) {
        std::memcpy(out.data() + sizeof(h) + i * 8, rows[i].data(), 8);
    }
    return out;
}

std::vector<uint8_t> build_arrangements_section(
    const std::vector<std::vector<uint8_t>>& arrangements) {
    std::vector<uint8_t> out;
    uint32_t count = static_cast<uint32_t>(arrangements.size());
    out.resize(4);
    std::memcpy(out.data(), &count, 4);
    for (const auto& a : arrangements) {
        uint64_t sz = a.size();
        auto old = out.size();
        out.resize(old + 8 + a.size());
        std::memcpy(out.data() + old, &sz, 8);
        if (!a.empty())
            std::memcpy(out.data() + old + 8, a.data(), a.size());
    }
    return out;
}

} // namespace mora::emit
