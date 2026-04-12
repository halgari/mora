#pragma once
#include "mora/data/chunk.h"
#include "mora/data/columnar_relation.h"

namespace mora {

// ---------------------------------------------------------------------------
// DataChunk — lightweight view of one chunk from a ColumnarRelation plus a
// selection vector identifying which rows are active.  Passed between pipeline
// operators on the stack; never owns any memory.
// ---------------------------------------------------------------------------
struct DataChunk {
    static constexpr size_t MAX_COLS = 8;

    ChunkBase* columns[MAX_COLS] = {};
    size_t col_count = 0;
    size_t row_count = 0;   // total rows in the underlying chunk buffers
    SelectionVector sel;    // which rows are active

    U32Chunk*       u32(size_t col)       { return static_cast<U32Chunk*>(columns[col]); }
    I64Chunk*       i64(size_t col)       { return static_cast<I64Chunk*>(columns[col]); }
    F64Chunk*       f64(size_t col)       { return static_cast<F64Chunk*>(columns[col]); }
    const U32Chunk* u32(size_t col) const { return static_cast<const U32Chunk*>(columns[col]); }
    const I64Chunk* i64(size_t col) const { return static_cast<const I64Chunk*>(columns[col]); }
    const F64Chunk* f64(size_t col) const { return static_cast<const F64Chunk*>(columns[col]); }
};

// ---------------------------------------------------------------------------
// Internal helper: fill dc.columns[] and dc.row_count for chunk ci of rel.
// Returns false if rel has no columns.
// ---------------------------------------------------------------------------
namespace detail {

inline bool fill_chunk(DataChunk& dc, const ColumnarRelation& rel, size_t ci) {
    dc.col_count = rel.arity();
    if (dc.col_count == 0) return false;

    // Determine row_count from the first column.
    const ChunkBase* first = nullptr;
    switch (rel.col_types()[0]) {
        case ColType::U32: first = rel.u32_chunk(0, ci); break;
        case ColType::I64: first = rel.i64_chunk(0, ci); break;
        case ColType::F64: first = rel.f64_chunk(0, ci); break;
    }
    dc.row_count = first->count;

    for (size_t c = 0; c < rel.arity(); c++) {
        switch (rel.col_types()[c]) {
            case ColType::U32:
                dc.columns[c] = const_cast<ChunkBase*>(
                    static_cast<const ChunkBase*>(rel.u32_chunk(c, ci)));
                break;
            case ColType::I64:
                dc.columns[c] = const_cast<ChunkBase*>(
                    static_cast<const ChunkBase*>(rel.i64_chunk(c, ci)));
                break;
            case ColType::F64:
                dc.columns[c] = const_cast<ChunkBase*>(
                    static_cast<const ChunkBase*>(rel.f64_chunk(c, ci)));
                break;
        }
    }
    return true;
}

} // namespace detail

// ---------------------------------------------------------------------------
// scan — iterate every row in rel, one chunk at a time.
// Callback signature: void(const DataChunk&)
// ---------------------------------------------------------------------------
template <typename F>
void scan(const ColumnarRelation& rel, F&& callback) {
    for (size_t ci = 0; ci < rel.chunk_count(); ci++) {
        DataChunk dc;
        if (!detail::fill_chunk(dc, rel, ci)) continue;
        dc.sel.select_all(dc.row_count);
        callback(dc);
    }
}

// ---------------------------------------------------------------------------
// scan_filtered — iterate only rows where the U32 column at filter_col equals
// filter_val.  The callback is skipped for chunks where nothing matches.
// Callback signature: void(const DataChunk&)
// ---------------------------------------------------------------------------
template <typename F>
void scan_filtered(const ColumnarRelation& rel, size_t filter_col,
                   uint32_t filter_val, F&& callback) {
    for (size_t ci = 0; ci < rel.chunk_count(); ci++) {
        DataChunk dc;
        if (!detail::fill_chunk(dc, rel, ci)) continue;

        const auto* fc = static_cast<const U32Chunk*>(dc.columns[filter_col]);

        dc.sel.count = 0;
        for (size_t i = 0; i < fc->count; i++) {
            if (fc->data[i] == filter_val) {
                dc.sel.indices[dc.sel.count++] = static_cast<uint16_t>(i);
            }
        }
        if (dc.sel.count > 0) {
            callback(dc);
        }
    }
}

} // namespace mora
