#include "mora/eval/op_antijoin.h"

#include <cassert>

namespace mora {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

AntiJoinOp::AntiJoinOp(std::unique_ptr<Operator> left,
                         std::unique_ptr<Operator> right,
                         std::vector<StringId>     shared_vars)
    : left_(std::move(left))
    , right_(std::move(right))
    , shared_vars_(std::move(shared_vars))
{
    assert(left_  != nullptr && "AntiJoinOp: left operand must not be null");
    assert(right_ != nullptr && "AntiJoinOp: right operand must not be null");
    assert(!shared_vars_.empty() &&
           "AntiJoinOp: shared_vars must be non-empty (anti-Cartesian not supported)");
}

// ---------------------------------------------------------------------------
// Hash helpers (same combine function as JoinOp)
// ---------------------------------------------------------------------------

static uint64_t hash_combine(uint64_t seed, uint64_t h) {
    return seed ^ (h + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

uint64_t AntiJoinOp::hash_shared(const BindingChunk& chunk, size_t row) const {
    uint64_t h = 0;
    for (auto const& sv : shared_vars_) {
        int const col = chunk.index_of(sv);
        assert(col >= 0 && "AntiJoinOp: shared var missing from chunk — bug in planner");
        h = hash_combine(h, chunk.cell(row, static_cast<size_t>(col)).hash());
    }
    return h;
}

bool AntiJoinOp::shared_match(const BindingChunk& right_chunk, size_t right_row,
                                const BindingChunk& left_chunk,  size_t left_row) const {
    for (auto const& sv : shared_vars_) {
        int const rc = right_chunk.index_of(sv);
        int const lc = left_chunk.index_of(sv);
        assert(rc >= 0 && lc >= 0 && "AntiJoinOp: shared var missing — bug in planner");
        if (!(right_chunk.cell(right_row, static_cast<size_t>(rc)) ==
              left_chunk.cell(left_row,   static_cast<size_t>(lc)))) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Build phase — drain right, index on shared vars
// ---------------------------------------------------------------------------

void AntiJoinOp::build_right() {
    built_ = true;

    while (auto chunk_opt = right_->next_chunk()) {
        right_chunks_.push_back(std::move(*chunk_opt));
    }

    // Build the hash index.
    for (uint32_t ci = 0; ci < static_cast<uint32_t>(right_chunks_.size()); ++ci) {
        const BindingChunk& chunk = right_chunks_[ci];
        for (uint32_t ri = 0; ri < static_cast<uint32_t>(chunk.row_count()); ++ri) {
            uint64_t const h = hash_shared(chunk, ri);
            index_[h].push_back({ci, ri});
        }
    }
}

// ---------------------------------------------------------------------------
// Probe-side existence check
// ---------------------------------------------------------------------------

bool AntiJoinOp::right_has_match(const BindingChunk& left_chunk,
                                   size_t left_row) const {
    uint64_t const h = hash_shared(left_chunk, left_row);
    auto it = index_.find(h);
    if (it == index_.end()) return false;

    for (auto const& [ci, ri] : it->second) {
        const BindingChunk& rc = right_chunks_[ci];
        if (shared_match(rc, ri, left_chunk, left_row)) {
            return true;  // a real match exists on the right → suppress this left row
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// next_chunk
// ---------------------------------------------------------------------------

std::optional<BindingChunk> AntiJoinOp::next_chunk() {
    if (!built_) build_right();

    // If right index is empty (zero right rows), every left row survives.
    // The general logic below handles this correctly: hash lookup always
    // misses → right_has_match returns false → row is always emitted.

    while (true) {
        auto left_opt = left_->next_chunk();
        if (!left_opt) return std::nullopt;

        const BindingChunk& left = *left_opt;

        // Build output chunk: same shape as left (right adds no columns).
        std::vector<const Type*> out_types;
        out_types.reserve(left.arity());
        for (size_t i = 0; i < left.arity(); ++i) {
            out_types.push_back(left.column(i).type());
        }
        BindingChunk out(left.names(), out_types);

        for (size_t r = 0; r < left.row_count(); ++r) {
            if (!right_has_match(left, r)) {
                // No match on right → emit this left row.
                std::vector<Value> row;
                row.reserve(left.arity());
                for (size_t col = 0; col < left.arity(); ++col) {
                    row.push_back(left.cell(r, col));
                }
                out.append_row(row);
            }
        }

        if (out.row_count() > 0) return out;
        // All rows in this left chunk had right-side matches — pull next.
    }
}

} // namespace mora
