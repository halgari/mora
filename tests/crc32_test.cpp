#include "mora/rt/crc32.h"
#include "mora/rt/bst_hashmap.h"

#include <gtest/gtest.h>

TEST(CRC32Test, Deterministic) {
    uint32_t a = mora::rt::hash_formid(0x00012EB7);
    uint32_t b = mora::rt::hash_formid(0x00012EB7);
    EXPECT_EQ(a, b);
}

TEST(CRC32Test, Different) {
    EXPECT_NE(mora::rt::hash_formid(0x100), mora::rt::hash_formid(0x200));
}

TEST(CRC32Test, KnownValue) {
    uint32_t hash = mora::rt::hash_formid(0x00012EB7);
    EXPECT_NE(hash, 0u);
}

TEST(BSTHashMapTest, LookupInMockMap) {
    const uint32_t CAPACITY = 8;

    // Sentinel
    static const mora::rt::BSTEntry sentinel_val = {0xDEAD, 0, nullptr, nullptr};

    // Entry array
    mora::rt::BSTEntry entries[CAPACITY] = {};

    // Insert FormID 0x100 -> fake pointer 0xAAAA
    uint32_t hash = mora::rt::hash_formid(0x100);
    uint32_t idx = hash & (CAPACITY - 1);
    entries[idx].key = 0x100;
    entries[idx].value = reinterpret_cast<void*>(0xAAAA);
    entries[idx].next = const_cast<mora::rt::BSTEntry*>(&sentinel_val);

    // Build map layout
    mora::rt::BSTHashMapLayout map = {};
    map.capacity = CAPACITY;
    map.sentinel = const_cast<mora::rt::BSTEntry*>(&sentinel_val);
    map.entries = entries;

    // Lookup should find it
    void* result = ::bst_hashmap_lookup(&map, 0x100);
    EXPECT_EQ(result, reinterpret_cast<void*>(0xAAAA));

    // Lookup miss
    void* miss = ::bst_hashmap_lookup(&map, 0x999);
    EXPECT_EQ(miss, nullptr);
}
