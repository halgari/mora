#include "mora/rt/bst_hashmap.h"
#include "mora/rt/crc32.h"

namespace mora::rt {

void* bst_hashmap_lookup(const BSTHashMapLayout* map, uint32_t formid) {
    if (!map || !map->entries || map->capacity == 0) return nullptr;

    uint32_t hash = bst_crc32(&formid, sizeof(formid));
    uint32_t idx = hash & (map->capacity - 1);

    BSTEntry* entry = &map->entries[idx];

    // nullptr next means empty slot
    if (entry->next == nullptr) return nullptr;

    // Walk the chain until we hit sentinel
    while (entry != map->sentinel) {
        if (entry->key == formid) return entry->value;
        entry = entry->next;
        if (!entry) return nullptr;  // safety
    }

    return nullptr;
}

} // namespace mora::rt
