#pragma once
#include <cstdint>
#include <cstddef>

namespace mora {

static constexpr size_t CHUNK_SIZE = 2048;

struct ChunkBase {
    size_t count = 0;  // actual rows (0..CHUNK_SIZE)
};

struct U32Chunk : ChunkBase {
    uint32_t data[CHUNK_SIZE];  // 8 KB — FormIDs, StringIds
};

struct I64Chunk : ChunkBase {
    int64_t data[CHUNK_SIZE];   // 16 KB
};

struct F64Chunk : ChunkBase {
    double data[CHUNK_SIZE];    // 16 KB
};

// Selection vector: indices into a chunk identifying which rows passed a filter.
// Stack-allocated. Passed between pipeline operators.
struct SelectionVector {
    uint16_t indices[CHUNK_SIZE];
    size_t count = 0;

    void select_all(size_t n) {
        for (size_t i = 0; i < n; i++) indices[i] = static_cast<uint16_t>(i);
        count = n;
    }
};

} // namespace mora
