#include "mora/eval/op_join.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace mora {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

JoinOp::JoinOp(std::unique_ptr<Operator> left,
               std::unique_ptr<Operator> right,
               std::vector<StringId>     shared_vars)
    : left_(std::move(left))
    , right_(std::move(right))
    , shared_vars_(std::move(shared_vars))
{
    assert(left_  != nullptr && "JoinOp: left operand must not be null");
    assert(right_ != nullptr && "JoinOp: right operand must not be null");

    // Compute left-only and right-only variable sets from the operators'
    // declared output_var_names(). The output layout is:
    //   shared_vars_ (left order) | left_only_ | right_only_
    //
    // NOTE: out_var_names_ / out_col_types_ are only fully populated after
    // build_left() runs (because we need actual Type* from the first chunk
    // for left-only and shared columns, and from the first right chunk for
    // right-only columns). We pre-compute the name ordering here; types are
    // filled in during build_left() + first probe chunk.

    // Build a set of shared-var indices for fast lookup.
    auto is_shared = [this](StringId id) -> bool {
        for (auto const& s : shared_vars_) {
            if (s.index == id.index) return true;
        }
        return false;
    };

    for (auto const& n : left_->output_var_names()) {
        if (!is_shared(n)) {
            left_only_.push_back(n);
        }
    }
    for (auto const& n : right_->output_var_names()) {
        if (!is_shared(n)) {
            right_only_.push_back(n);
        }
    }

    // Pre-fill the name ordering (types come later from actual chunks).
    out_var_names_.reserve(shared_vars_.size() + left_only_.size() + right_only_.size());
    for (auto const& n : shared_vars_) out_var_names_.push_back(n);
    for (auto const& n : left_only_)   out_var_names_.push_back(n);
    for (auto const& n : right_only_)  out_var_names_.push_back(n);
}

// ---------------------------------------------------------------------------
// Hash helpers
// ---------------------------------------------------------------------------

// Combine two 64-bit hash values (boost::hash_combine-inspired, 64-bit).
static uint64_t hash_combine(uint64_t seed, uint64_t h) {
    return seed ^ (h + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

uint64_t JoinOp::hash_shared(const BindingChunk& chunk, size_t row) const {
    uint64_t h = 0;
    for (auto const& sv : shared_vars_) {
        int const col = chunk.index_of(sv);
        assert(col >= 0 && "JoinOp: shared var missing from chunk — bug in planner");
        h = hash_combine(h, chunk.cell(row, static_cast<size_t>(col)).hash());
    }
    return h;
}

bool JoinOp::shared_match(const BindingChunk& left_chunk,  size_t left_row,
                           const BindingChunk& right_chunk, size_t right_row) const {
    for (auto const& sv : shared_vars_) {
        int const lc = left_chunk.index_of(sv);
        int const rc = right_chunk.index_of(sv);
        assert(lc >= 0 && rc >= 0 && "JoinOp: shared var missing — bug in planner");
        if (!(left_chunk.cell(left_row,  static_cast<size_t>(lc)) ==
              right_chunk.cell(right_row, static_cast<size_t>(rc)))) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Build phase
// ---------------------------------------------------------------------------

void JoinOp::build_left() {
    built_ = true;

    // Drain the left operator.
    while (auto chunk_opt = left_->next_chunk()) {
        left_chunks_.push_back(std::move(*chunk_opt));
    }

    if (left_chunks_.empty()) {
        // No left rows — join will always produce nothing. Populate types
        // with placeholders so the output BindingChunk can still be built.
        return;
    }

    // Derive types from the first left chunk.
    const BindingChunk& first = left_chunks_[0];
    out_col_types_.reserve(out_var_names_.size());

    // Shared vars — types from left.
    for (auto const& sv : shared_vars_) {
        int const col = first.index_of(sv);
        assert(col >= 0);
        out_col_types_.push_back(first.column(static_cast<size_t>(col)).type());
    }
    // Left-only vars — types from left.
    for (auto const& lv : left_only_) {
        int const col = first.index_of(lv);
        assert(col >= 0);
        out_col_types_.push_back(first.column(static_cast<size_t>(col)).type());
    }
    // Right-only var types are unknown until we see the first right chunk;
    // reserve space and fill in build_right_types() on first probe chunk.
    // For now leave them absent — we'll push them when we see right.

    // Build the hash index: for each left chunk, for each row, hash shared vars.
    for (uint32_t ci = 0; ci < static_cast<uint32_t>(left_chunks_.size()); ++ci) {
        const BindingChunk& chunk = left_chunks_[ci];
        for (uint32_t ri = 0; ri < static_cast<uint32_t>(chunk.row_count()); ++ri) {
            uint64_t const h = hash_shared(chunk, ri);
            index_[h].push_back({ci, ri});
        }
    }
}

// ---------------------------------------------------------------------------
// Probe row emission
// ---------------------------------------------------------------------------

void JoinOp::probe_row(const BindingChunk& probe, size_t probe_row,
                        BindingChunk& out) const {
    uint64_t const h = hash_shared(probe, probe_row);
    auto it = index_.find(h);
    if (it == index_.end()) return;

    for (auto const& [ci, ri] : it->second) {
        const BindingChunk& lc = left_chunks_[ci];

        // Collision check: verify all shared vars actually match.
        if (!shared_match(lc, ri, probe, probe_row)) continue;

        // Build the output row: shared vars, then left-only, then right-only.
        std::vector<Value> row;
        row.reserve(out_var_names_.size());

        for (auto const& sv : shared_vars_) {
            int const col = lc.index_of(sv);
            row.push_back(lc.cell(ri, static_cast<size_t>(col)));
        }
        for (auto const& lv : left_only_) {
            int const col = lc.index_of(lv);
            row.push_back(lc.cell(ri, static_cast<size_t>(col)));
        }
        for (auto const& rv : right_only_) {
            int const col = probe.index_of(rv);
            row.push_back(probe.cell(probe_row, static_cast<size_t>(col)));
        }

        out.append_row(row);
    }
}

// ---------------------------------------------------------------------------
// next_chunk
// ---------------------------------------------------------------------------

std::optional<BindingChunk> JoinOp::next_chunk() {
    if (!built_) build_left();

    // If build produced no left rows, this join is empty.
    if (left_chunks_.empty()) return std::nullopt;

    // Pull probe chunks from right until we produce a non-empty output chunk
    // or right is exhausted.
    while (true) {
        auto probe_opt = right_->next_chunk();
        if (!probe_opt) return std::nullopt;

        const BindingChunk& probe = *probe_opt;

        // On first right chunk, finish populating out_col_types_ with
        // right-only column types. This is safe to repeat (idempotent after
        // first call since right_only_ count is fixed).
        if (out_col_types_.size() < out_var_names_.size()) {
            for (auto const& rv : right_only_) {
                int const col = probe.index_of(rv);
                assert(col >= 0 && "JoinOp: right-only var missing from probe chunk");
                out_col_types_.push_back(probe.column(static_cast<size_t>(col)).type());
            }
        }

        // Build output chunk.
        BindingChunk out(out_var_names_, out_col_types_);
        for (size_t r = 0; r < probe.row_count(); ++r) {
            probe_row(probe, r, out);
        }

        if (out.row_count() > 0) return out;
        // Else: this probe chunk produced no matches — pull next.
    }
}

} // namespace mora
