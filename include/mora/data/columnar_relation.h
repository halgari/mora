#pragma once
#include "mora/data/chunk.h"
#include "mora/data/chunk_pool.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <initializer_list>

namespace mora {

enum class ColType : uint8_t { U32, I64, F64 };

struct RowRef {
    uint32_t chunk_idx;
    uint16_t row_idx;
};

class ColumnarRelation {
public:
    ColumnarRelation(size_t arity, std::vector<ColType> types, ChunkPool& pool);

    // Append a single row. Values passed as uint64_t, cast per column type.
    void append_row(const uint64_t* values);
    // Convenience overload
    void append_row(std::initializer_list<uint64_t> values);

    // Build a hash index on a column (for U32 columns — FormIDs, StringIds)
    void build_index(size_t col);

    // Lookup rows where U32 column col == value. Returns empty if no index or no match.
    const std::vector<RowRef>& lookup(size_t col, uint32_t value) const;

    size_t row_count() const { return total_rows_; }
    size_t chunk_count() const;
    size_t arity() const { return arity_; }
    const std::vector<ColType>& col_types() const { return col_types_; }

    // Direct chunk access (const and non-const)
    U32Chunk* u32_chunk(size_t col, size_t chunk_idx);
    I64Chunk* i64_chunk(size_t col, size_t chunk_idx);
    F64Chunk* f64_chunk(size_t col, size_t chunk_idx);
    const U32Chunk* u32_chunk(size_t col, size_t chunk_idx) const;
    const I64Chunk* i64_chunk(size_t col, size_t chunk_idx) const;
    const F64Chunk* f64_chunk(size_t col, size_t chunk_idx) const;

private:
    void ensure_current_chunk();

    size_t arity_;
    std::vector<ColType> col_types_;
    ChunkPool& pool_;
    size_t total_rows_ = 0;
    size_t current_chunk_row_ = 0; // position within current (last) chunk

    // col_chunks_[col_index] = vector of chunk pointers for that column
    std::vector<std::vector<ChunkBase*>> col_chunks_;

    // indexes_[col_index] = hash map of value → vector<RowRef>
    // Only populated for columns where build_index() was called.
    std::vector<std::unordered_map<uint32_t, std::vector<RowRef>>> indexes_;
    static const std::vector<RowRef> empty_refs_;
};

} // namespace mora
