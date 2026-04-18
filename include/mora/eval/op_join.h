#pragma once

#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mora {

// Hash-join two operator streams on shared variable columns. The build
// side is the "left" operator — fully drained and indexed on the first
// next_chunk() call. The probe side is "right" — iterated chunk-at-a-
// time; for each probe row the hash index is consulted and all matching
// left rows are emitted.
//
// Constructor takes explicit shared_vars: the planner computes the
// intersection of left->output_var_names() and right->output_var_names()
// and passes it in.
//
// Output column layout (stable): shared vars first (in the order they
// appear in left), then left-only vars, then right-only vars.
//
// Hash collision safety: every candidate from the index is verified by
// comparing Value equality on each shared var before emitting — same
// approach as ColumnarRelation::lookup.
class JoinOp : public Operator {
public:
    JoinOp(std::unique_ptr<Operator>   left,
           std::unique_ptr<Operator>   right,
           std::vector<StringId>       shared_vars);

    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override {
        return out_var_names_;
    }

private:
    std::unique_ptr<Operator>  left_;
    std::unique_ptr<Operator>  right_;

    std::vector<StringId>      shared_vars_;   // vars present in both left and right
    std::vector<StringId>      left_only_;     // vars present only in left
    std::vector<StringId>      right_only_;    // vars present only in right
    std::vector<StringId>      out_var_names_; // shared + left_only + right_only
    std::vector<const Type*>   out_col_types_; // parallel to out_var_names_

    // Build phase: left chunks + hash index.
    // Index key = combined hash of shared-var cell values for that row.
    // Index value = list of (chunk_idx, row_idx) pairs.
    std::vector<BindingChunk>                              left_chunks_;
    std::unordered_map<uint64_t,
        std::vector<std::pair<uint32_t, uint32_t>>>        index_;
    bool built_ = false;

    // Drain left_, build left_chunks_ and index_. Called lazily on first
    // next_chunk(). Computes out_var_names_ / out_col_types_ from the
    // first left chunk (if any).
    void build_left();

    // Hash the shared-var cells for a single row in a chunk.
    uint64_t hash_shared(const BindingChunk& chunk, size_t row) const;

    // Verify that all shared vars match between a left row and probe row
    // (filters hash collisions).
    bool shared_match(const BindingChunk& left_chunk,  size_t left_row,
                      const BindingChunk& right_chunk, size_t right_row) const;

    // Emit all joined rows for probe_row into `out`.
    void probe_row(const BindingChunk& probe, size_t probe_row,
                   BindingChunk& out) const;
};

} // namespace mora
