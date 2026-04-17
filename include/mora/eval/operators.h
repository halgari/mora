#pragma once
#include "mora/eval/pipeline.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/chunk_pool.h"

namespace mora {

// ---------------------------------------------------------------------------
// HashProbe: join input chunk with a build relation via hash index.
//
// For each selected row in input, probes build_rel's index on build_key_col
// using the value from input's probe_col. Produces output chunks with
// input columns + build relation's columns appended.
//
// Output chunks are acquired from pool. When full (CHUNK_SIZE rows),
// flush to sink and acquire a new one.
// ---------------------------------------------------------------------------
template <typename Sink>
void hash_probe(const ColumnarRelation& build_rel,
                size_t probe_col,
                size_t build_key_col,
                const DataChunk& input,
                ChunkPool& pool,
                Sink&& sink) {
    size_t const out_col_count = input.col_count + build_rel.arity();

    // Acquire output column chunks (all U32 for now)
    std::vector<U32Chunk*> out_chunks(out_col_count);
    for (size_t c = 0; c < out_col_count; c++) {
        out_chunks[c] = pool.acquire_u32();
    }
    size_t out_row = 0;

    auto flush = [&]() {
        if (out_row == 0) return;
        DataChunk out;
        out.col_count = out_col_count;
        out.row_count = out_row;
        for (size_t c = 0; c < out_col_count; c++) {
            out_chunks[c]->count = out_row;
            out.columns[c] = out_chunks[c];
        }
        out.sel.select_all(out_row);
        sink(out);
        // Acquire fresh output chunks (don't release - sink may still reference them)
        for (size_t c = 0; c < out_col_count; c++) {
            out_chunks[c] = pool.acquire_u32();
        }
        out_row = 0;
    };

    for (size_t si = 0; si < input.sel.count; si++) {
        auto row = input.sel.indices[si];
        uint32_t const probe_val = input.u32(probe_col)->data[row];

        const auto& refs = build_rel.lookup(build_key_col, probe_val);
        for (const auto& ref : refs) {
            // Copy input columns
            for (size_t c = 0; c < input.col_count; c++) {
                out_chunks[c]->data[out_row] = input.u32(c)->data[row];
            }
            // Copy build columns
            for (size_t c = 0; c < build_rel.arity(); c++) {
                auto* build_chunk = build_rel.u32_chunk(c, ref.chunk_idx);
                out_chunks[input.col_count + c]->data[out_row] =
                    build_chunk->data[ref.row_idx];
            }
            out_row++;
            if (out_row == CHUNK_SIZE) flush();
        }
    }
    flush(); // emit remaining

    // Release the last set of output chunks (unused after flush allocated fresh ones)
    for (auto* c : out_chunks) pool.release(c);
}

// ---------------------------------------------------------------------------
// SemiJoin: filter input to rows present in an index.
//
// Rewrites input.sel in-place: keep only rows where input[input_col]
// exists in rel's index on key_col. Zero allocation.
// ---------------------------------------------------------------------------
template <typename Sink>
void semi_join(const ColumnarRelation& rel,
               size_t key_col,
               size_t input_col,
               DataChunk& input,
               Sink&& sink) {
    auto* col = input.u32(input_col);
    SelectionVector new_sel;
    new_sel.count = 0;
    for (size_t i = 0; i < input.sel.count; i++) {
        auto row = input.sel.indices[i];
        uint32_t const val = col->data[row];
        if (!rel.lookup(key_col, val).empty()) {
            new_sel.indices[new_sel.count++] = row;
        }
    }
    input.sel = new_sel;
    if (input.sel.count > 0) {
        sink(input);
    }
}

} // namespace mora
