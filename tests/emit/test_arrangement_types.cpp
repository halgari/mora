#include "mora/emit/arrangement.h"
#include <gtest/gtest.h>

using namespace mora::emit;

TEST(Arrangement, HeaderIs16Bytes) {
    static_assert(sizeof(ArrangementHeader) == 16);
    EXPECT_EQ(sizeof(ArrangementHeader), 16u);
}

TEST(Arrangement, DefaultHeaderHasZeroRows) {
    ArrangementHeader h{};
    EXPECT_EQ(h.row_count, 0u);
    EXPECT_EQ(h.row_stride_bytes, 0u);
    EXPECT_EQ(h.key_column_index, 0u);
}
