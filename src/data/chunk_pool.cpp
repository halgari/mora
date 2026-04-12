#include "mora/data/chunk_pool.h"

namespace mora {

U32Chunk* ChunkPool::acquire_u32() {
    if (!free_u32_.empty()) {
        U32Chunk* c = free_u32_.back();
        free_u32_.pop_back();
        c->count = 0;
        return c;
    }
    auto owned = std::make_unique<U32Chunk>();
    U32Chunk* raw = owned.get();
    owned_.push_back(std::move(owned));
    return raw;
}

I64Chunk* ChunkPool::acquire_i64() {
    if (!free_i64_.empty()) {
        I64Chunk* c = free_i64_.back();
        free_i64_.pop_back();
        c->count = 0;
        return c;
    }
    auto owned = std::make_unique<I64Chunk>();
    I64Chunk* raw = owned.get();
    owned_.push_back(std::move(owned));
    return raw;
}

F64Chunk* ChunkPool::acquire_f64() {
    if (!free_f64_.empty()) {
        F64Chunk* c = free_f64_.back();
        free_f64_.pop_back();
        c->count = 0;
        return c;
    }
    auto owned = std::make_unique<F64Chunk>();
    F64Chunk* raw = owned.get();
    owned_.push_back(std::move(owned));
    return raw;
}

} // namespace mora
