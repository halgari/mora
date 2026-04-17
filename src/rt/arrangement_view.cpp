#include "mora/rt/arrangement_view.h"
#include <cstring>

namespace mora::rt {

ArrangementView::ArrangementView(const uint8_t* data, size_t size) {
    if (size >= sizeof(header_)) {
        std::memcpy(&header_, data, sizeof(header_));
        rows_      = data + sizeof(header_);
        rows_size_ = size - sizeof(header_);
    }
}

U32RowRange ArrangementView::equal_range_u32(uint32_t key) const {
    const uint16_t stride_b = header_.row_stride_bytes;
    if (stride_b == 0) return {nullptr, 0, 0};
    const size_t cols = stride_b / 4;
    const uint32_t* rows = reinterpret_cast<const uint32_t*>(rows_);
    const uint32_t count = header_.row_count;
    const uint8_t  k_col = header_.key_column_index;

    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t const mid = (lo + hi) / 2;
        if (rows[(mid * cols) + k_col] < key) lo = mid + 1;
        else                                hi = mid;
    }
    const size_t first = lo;
    size_t last = first;
    while (last < count && rows[(last * cols) + k_col] == key) ++last;

    return { rows + (first * cols), last - first, static_cast<uint16_t>(cols) };
}

} // namespace mora::rt
