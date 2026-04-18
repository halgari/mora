#include "mora/eval/op_union.h"

#include <cassert>

namespace mora {

UnionOp::UnionOp(std::unique_ptr<Operator> left,
                  std::unique_ptr<Operator> right)
    : left_(std::move(left))
    , right_(std::move(right))
{
    // Both sides must expose the same output var names (same order and identity)
    // so that downstream consumers see a consistent schema.
    left_var_names_ = left_->output_var_names();
    const auto& right_names = right_->output_var_names();
    assert(left_var_names_.size() == right_names.size()
        && "UnionOp: left and right output var names must match in size");
    for (size_t i = 0; i < left_var_names_.size(); ++i) {
        assert(left_var_names_[i].index == right_names[i].index
            && "UnionOp: left and right output var names must match");
    }
}

std::optional<BindingChunk> UnionOp::next_chunk() {
    if (!left_exhausted_) {
        auto chunk = left_->next_chunk();
        if (chunk) return chunk;
        left_exhausted_ = true;
    }
    return right_->next_chunk();
}

} // namespace mora
