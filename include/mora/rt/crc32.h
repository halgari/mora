#pragma once

#include <cstddef>
#include <cstdint>

namespace mora::rt {

/// Standard CRC32 (polynomial 0xEDB88320) as used by Skyrim's BSTHashMap.
uint32_t bst_crc32(const void* data, size_t len);

/// Convenience: hash a FormID for BSTHashMap bucket lookup.
inline uint32_t hash_formid(uint32_t formid) {
    return bst_crc32(&formid, sizeof(formid));
}

} // namespace mora::rt
