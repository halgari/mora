#pragma once

#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"

#include <memory>
#include <optional>

namespace mora {

// Concatenates two operator streams. Emits all chunks from `left`, then
// all chunks from `right`. Both sides must produce compatible output var
// names (same names, same order) — asserted in the constructor.
class UnionOp : public Operator {
public:
    UnionOp(std::unique_ptr<Operator> left,
            std::unique_ptr<Operator> right);

    std::optional<BindingChunk> next_chunk() override;

    const std::vector<StringId>& output_var_names() const override {
        return left_var_names_;
    }

private:
    std::unique_ptr<Operator> left_;
    std::unique_ptr<Operator> right_;
    std::vector<StringId>     left_var_names_;
    bool                      left_exhausted_ = false;
};

} // namespace mora
