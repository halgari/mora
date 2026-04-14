#pragma once
#include "mora/emit/arrangement.h"
#include <cstddef>
#include <cstdint>

namespace mora::rt {

struct U32RowRange {
    const uint32_t* rows;    // pointer to first matching row (columns contiguous)
    size_t          count;   // number of matching rows
    uint16_t        stride_u32;  // columns per row
};

class ArrangementView {
public:
    ArrangementView(const uint8_t* data, size_t size);
    U32RowRange equal_range_u32(uint32_t key) const;
    const emit::ArrangementHeader& header() const { return header_; }

private:
    emit::ArrangementHeader header_{};
    const uint8_t*          rows_ = nullptr;
    size_t                  rows_size_ = 0;
};

} // namespace mora::rt
