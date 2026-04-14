#pragma once
#include <cstdint>
#include <array>

namespace mora::emit {

enum class SectionId : uint32_t {
    None          = 0,
    StringTable   = 1,
    Keywords      = 2,
    Patches       = 3,
    Arrangements  = 4,
    DagBytecode   = 5,
    Manifest      = 6,
};

struct PatchFileV2Header {
    uint32_t magic          = 0x41524F4D;  // 'MORA' little-endian
    uint32_t version        = 4;
    uint32_t flags          = 0;
    uint32_t section_count  = 0;
    uint64_t file_size      = 0;
    uint64_t toolchain_id   = 0;
    std::array<uint8_t, 32> esp_digest{};
};
static_assert(sizeof(PatchFileV2Header) == 64);

struct SectionDirectoryEntry {
    uint32_t section_id;
    uint32_t flags;
    uint64_t offset;
    uint64_t size;
};
static_assert(sizeof(SectionDirectoryEntry) == 24);

} // namespace mora::emit
