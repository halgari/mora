#pragma once

#include <cstdint>

namespace mora::rt {

struct BSTEntry {
    uint32_t key;       // 0x00: FormID
    uint32_t pad;       // 0x04
    void*    value;     // 0x08: TESForm*
    BSTEntry* next;     // 0x10: nullptr=empty, sentinel=end of chain
};
static_assert(sizeof(BSTEntry) == 0x18);

struct BSTHashMapLayout {
    uint64_t  pad00;       // 0x00
    uint32_t  pad08;       // 0x08
    uint32_t  capacity;    // 0x0C (always power of 2)
    uint32_t  free_count;  // 0x10
    uint32_t  good;        // 0x14
    BSTEntry* sentinel;    // 0x18
    uint64_t  alloc_pad;   // 0x20
    BSTEntry* entries;     // 0x28
};

} // namespace mora::rt

// Exposed as extern "C" for clean LLVM IR linkage
extern "C" void* bst_hashmap_lookup(const mora::rt::BSTHashMapLayout* map, uint32_t formid);
