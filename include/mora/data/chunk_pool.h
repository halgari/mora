#pragma once
#include "mora/data/chunk.h"
#include <vector>
#include <memory>

namespace mora {

class ChunkPool {
public:
    U32Chunk* acquire_u32();
    I64Chunk* acquire_i64();
    F64Chunk* acquire_f64();

    void release(U32Chunk* c) { free_u32_.push_back(c); }
    void release(I64Chunk* c) { free_i64_.push_back(c); }
    void release(F64Chunk* c) { free_f64_.push_back(c); }

private:
    std::vector<U32Chunk*> free_u32_;
    std::vector<I64Chunk*> free_i64_;
    std::vector<F64Chunk*> free_f64_;
    std::vector<std::unique_ptr<ChunkBase>> owned_;
};

} // namespace mora
