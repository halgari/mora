#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>

using namespace mora::emit;

TEST(PatchFileV2, HeaderIsSixtyFourBytes) {
    static_assert(sizeof(PatchFileV2Header) == 64);
    EXPECT_EQ(sizeof(PatchFileV2Header), 64u);
}

TEST(PatchFileV2, SectionEntryIsTwentyFourBytes) {
    static_assert(sizeof(SectionDirectoryEntry) == 24);
    EXPECT_EQ(sizeof(SectionDirectoryEntry), 24u);
}

TEST(PatchFileV2, MagicIsMORA) {
    PatchFileV2Header h{};
    EXPECT_EQ(h.magic, 0x41524F4Du);
}

TEST(PatchFileV2, VersionStartsAtFour) {
    PatchFileV2Header h{};
    EXPECT_EQ(h.version, 4u);
}

TEST(PatchFileV2, SectionIdsAreDistinct) {
    EXPECT_NE(static_cast<uint32_t>(SectionId::StringTable),
              static_cast<uint32_t>(SectionId::Patches));
    EXPECT_NE(static_cast<uint32_t>(SectionId::Patches),
              static_cast<uint32_t>(SectionId::Arrangements));
    EXPECT_NE(static_cast<uint32_t>(SectionId::Arrangements),
              static_cast<uint32_t>(SectionId::Keywords));
}
