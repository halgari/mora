#pragma once

#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mora {

// Anti-join two operator streams on shared variable columns. Emits left rows
// IFF the right side has NO match on the shared vars.
//
// Build phase drains the RIGHT operator and indexes on shared-var cell hashes.
// Probe phase reads the LEFT operator chunk-at-a-time; for each left row, if no
// right-index entry matches, the left row is emitted as-is.
//
// output_var_names() returns left_->output_var_names() — right columns do not
// appear in the output.
//
// Hash collision safety: every candidate from the index is verified by comparing
// Value equality on each shared var before declaring a match (same as JoinOp /
// ColumnarRelation).
//
// Anti-Cartesian (no shared vars between left and right) is rejected by the
// planner — the constructor asserts shared_vars is non-empty.
class AntiJoinOp : public Operator {
public:
    AntiJoinOp(std::unique_ptr<Operator> left,
               std::unique_ptr<Operator> right,
               std::vector<StringId>     shared_vars);

    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override {
        return left_->output_var_names();
    }

private:
    std::unique_ptr<Operator>  left_;
    std::unique_ptr<Operator>  right_;

    std::vector<StringId>  shared_vars_;   // vars present in both left and right

    // Build phase: right chunks + hash index.
    // Index key = combined hash of shared-var cell values for that row.
    // Index value = list of (chunk_idx, row_idx) pairs.
    std::vector<BindingChunk>                              right_chunks_;
    std::unordered_map<uint64_t,
        std::vector<std::pair<uint32_t, uint32_t>>>        index_;
    bool built_ = false;

    // Drain right_, build right_chunks_ and index_.  Called lazily on first
    // next_chunk().
    void build_right();

    // Hash the shared-var cells for a single row in a chunk.
    uint64_t hash_shared(const BindingChunk& chunk, size_t row) const;

    // Verify that all shared vars match between a right (index) row and a left
    // (probe) row.  Returns true iff all shared vars are equal — i.e. a match
    // EXISTS on the right, meaning this left row should be EXCLUDED.
    bool shared_match(const BindingChunk& right_chunk, size_t right_row,
                      const BindingChunk& left_chunk,  size_t left_row) const;

    // Returns true iff the left row at `left_row` in `left_chunk` has at least
    // one matching row on the right side (i.e. the row should be suppressed).
    bool right_has_match(const BindingChunk& left_chunk, size_t left_row) const;
};

} // namespace mora
