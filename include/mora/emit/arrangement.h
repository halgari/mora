#pragma once
#include <cstdint>

namespace mora::emit {

struct ArrangementHeader {
    uint32_t relation_id      = 0;
    uint32_t row_count        = 0;
    uint16_t row_stride_bytes = 0;
    uint8_t  key_column_index = 0;
    uint8_t  key_type         = 0;
    uint8_t  flags            = 0;
    uint8_t  reserved[3]      = {0, 0, 0};
};
static_assert(sizeof(ArrangementHeader) == 16);

} // namespace mora::emit
