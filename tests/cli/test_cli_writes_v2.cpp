// Minimal smoke test: the CLI writes mora_patches.bin by calling
// serialize_patch_table() and dumping the bytes. As of Task P2.4 that
// function produces the v2 sectioned format, so the CLI is v2 by
// construction. This test pins the function's header output so any
// regression is caught without needing to run the full CLI pipeline
// (which requires ESP data).
#include "mora/emit/patch_table.h"
#include "mora/emit/patch_file_v2.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace mora;

TEST(CliWritesV2, DirectCallProducesV2) {
    std::vector<PatchEntry> entries = { {0x1u, 0, 0, 1, 0, 0u} };
    auto bytes = serialize_patch_table(entries);
    emit::PatchFileV2Header h;
    std::memcpy(&h, bytes.data(), sizeof(h));
    EXPECT_EQ(h.magic, 0x41524F4Du);
    EXPECT_EQ(h.version, 4u);
}
