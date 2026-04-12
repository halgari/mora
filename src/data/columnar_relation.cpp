#include "mora/data/columnar_relation.h"
#include <cassert>
#include <cstring>

namespace mora {

const std::vector<RowRef> ColumnarRelation::empty_refs_;

ColumnarRelation::ColumnarRelation(size_t arity, std::vector<ColType> types, ChunkPool& pool)
    : arity_(arity), col_types_(std::move(types)), pool_(pool)
{
    assert(col_types_.size() == arity_);
    col_chunks_.resize(arity_);
    indexes_.resize(arity_);
    // Start with current_chunk_row_ == CHUNK_SIZE so the first append triggers
    // ensure_current_chunk().
    current_chunk_row_ = CHUNK_SIZE;
}

void ColumnarRelation::ensure_current_chunk() {
    for (size_t col = 0; col < arity_; ++col) {
        ChunkBase* chunk = nullptr;
        switch (col_types_[col]) {
            case ColType::U32: chunk = pool_.acquire_u32(); break;
            case ColType::I64: chunk = pool_.acquire_i64(); break;
            case ColType::F64: chunk = pool_.acquire_f64(); break;
        }
        col_chunks_[col].push_back(chunk);
    }
    current_chunk_row_ = 0;
}

void ColumnarRelation::append_row(const uint64_t* values) {
    if (current_chunk_row_ == CHUNK_SIZE) {
        ensure_current_chunk();
    }

    for (size_t col = 0; col < arity_; ++col) {
        ChunkBase* base = col_chunks_[col].back();
        switch (col_types_[col]) {
            case ColType::U32: {
                auto* chunk = static_cast<U32Chunk*>(base);
                chunk->data[current_chunk_row_] = static_cast<uint32_t>(values[col]);
                break;
            }
            case ColType::I64: {
                auto* chunk = static_cast<I64Chunk*>(base);
                chunk->data[current_chunk_row_] = static_cast<int64_t>(values[col]);
                break;
            }
            case ColType::F64: {
                auto* chunk = static_cast<F64Chunk*>(base);
                double d;
                std::memcpy(&d, &values[col], sizeof(double));
                chunk->data[current_chunk_row_] = d;
                break;
            }
        }
        base->count++;
    }

    ++current_chunk_row_;
    ++total_rows_;
}

void ColumnarRelation::append_row(std::initializer_list<uint64_t> values) {
    assert(values.size() == arity_);
    // Copy to a local buffer so we can pass a pointer.
    // Stack allocation is fine; arity is small.
    const uint64_t* ptr = values.begin();
    append_row(ptr);
}

size_t ColumnarRelation::chunk_count() const {
    if (arity_ == 0 || col_chunks_.empty()) return 0;
    return col_chunks_[0].size();
}

void ColumnarRelation::build_index(size_t col) {
    assert(col < arity_);
    assert(col_types_[col] == ColType::U32 && "build_index only supported for U32 columns");

    auto& idx = indexes_[col];
    idx.clear();

    const size_t n_chunks = col_chunks_[col].size();
    for (size_t ci = 0; ci < n_chunks; ++ci) {
        const auto* chunk = static_cast<const U32Chunk*>(col_chunks_[col][ci]);
        for (size_t ri = 0; ri < chunk->count; ++ri) {
            uint32_t val = chunk->data[ri];
            idx[val].push_back(RowRef{
                static_cast<uint32_t>(ci),
                static_cast<uint16_t>(ri)
            });
        }
    }
}

const std::vector<RowRef>& ColumnarRelation::lookup(size_t col, uint32_t value) const {
    assert(col < arity_);
    const auto& idx = indexes_[col];
    if (idx.empty()) return empty_refs_;
    auto it = idx.find(value);
    if (it == idx.end()) return empty_refs_;
    return it->second;
}

// --- chunk accessors ---

U32Chunk* ColumnarRelation::u32_chunk(size_t col, size_t chunk_idx) {
    assert(col < arity_);
    assert(col_types_[col] == ColType::U32);
    return static_cast<U32Chunk*>(col_chunks_[col][chunk_idx]);
}

I64Chunk* ColumnarRelation::i64_chunk(size_t col, size_t chunk_idx) {
    assert(col < arity_);
    assert(col_types_[col] == ColType::I64);
    return static_cast<I64Chunk*>(col_chunks_[col][chunk_idx]);
}

F64Chunk* ColumnarRelation::f64_chunk(size_t col, size_t chunk_idx) {
    assert(col < arity_);
    assert(col_types_[col] == ColType::F64);
    return static_cast<F64Chunk*>(col_chunks_[col][chunk_idx]);
}

const U32Chunk* ColumnarRelation::u32_chunk(size_t col, size_t chunk_idx) const {
    assert(col < arity_);
    assert(col_types_[col] == ColType::U32);
    return static_cast<const U32Chunk*>(col_chunks_[col][chunk_idx]);
}

const I64Chunk* ColumnarRelation::i64_chunk(size_t col, size_t chunk_idx) const {
    assert(col < arity_);
    assert(col_types_[col] == ColType::I64);
    return static_cast<const I64Chunk*>(col_chunks_[col][chunk_idx]);
}

const F64Chunk* ColumnarRelation::f64_chunk(size_t col, size_t chunk_idx) const {
    assert(col < arity_);
    assert(col_types_[col] == ColType::F64);
    return static_cast<const F64Chunk*>(col_chunks_[col][chunk_idx]);
}

} // namespace mora
