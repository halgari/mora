#include <gtest/gtest.h>
#include "mora/ext/runtime_index.h"

TEST(RuntimeIndexTest, ZeroDescriptorReturnsZero) {
    EXPECT_EQ(mora::ext::globalize_formid(0u, 0x12AB), 0u);
    EXPECT_EQ(mora::ext::globalize_formid(0u, 0x0),    0u);
}

TEST(RuntimeIndexTest, RegularEspEncoding) {
    // Runtime index 0x42, local id 0x12AB → 0x420012AB.
    EXPECT_EQ(mora::ext::globalize_formid(0x42u, 0x12ABu), 0x420012ABu);
    // Runtime index 0x01, local 0x123456 → 0x01123456.
    EXPECT_EQ(mora::ext::globalize_formid(0x01u, 0x00123456u), 0x01123456u);
    // High bits of local id are masked off (should not leak into the index byte).
    EXPECT_EQ(mora::ext::globalize_formid(0x10u, 0xFF000001u), 0x10000001u);
}

TEST(RuntimeIndexTest, EslEncoding) {
    // ESL slot 0x003, local 0x07F → 0xFE00307F.
    uint32_t desc = 0x003u | mora::ext::kRuntimeIdxEsl;
    EXPECT_EQ(mora::ext::globalize_formid(desc, 0x07Fu), 0xFE00307Fu);

    // Slot 0, local 0 → 0xFE000000 (reserved but well-defined).
    uint32_t desc0 = 0x000u | mora::ext::kRuntimeIdxEsl;
    EXPECT_EQ(mora::ext::globalize_formid(desc0, 0u), 0xFE000000u);

    // Slot 0xFFF (max), local 0xFFF (max) → 0xFEFFFFFF.
    uint32_t max_desc = 0xFFFu | mora::ext::kRuntimeIdxEsl;
    EXPECT_EQ(mora::ext::globalize_formid(max_desc, 0xFFFu), 0xFEFFFFFFu);

    // Local id high bits beyond 0xFFF are masked (ESL local space is 12 bits).
    EXPECT_EQ(mora::ext::globalize_formid(desc, 0xFFFFu), 0xFE003FFFu);
}

TEST(RuntimeIndexTest, EspIndexTopByteOnly) {
    // Only low 8 bits of a non-ESL descriptor are interpreted as the index.
    // This keeps 0x1xx descriptors well-defined even though real load orders
    // cap at 0xFD — paranoia masking, essentially.
    EXPECT_EQ(mora::ext::globalize_formid(0xABu, 0x012345u), 0xAB012345u);
}
