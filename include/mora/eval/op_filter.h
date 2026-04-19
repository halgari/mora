#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/eval/operator.h"

#include <memory>
#include <unordered_map>

namespace mora {

// Per-row expression filter. Pulls chunks from `input`, evaluates
// `predicate` for each row (with a fresh Bindings map built from the
// row's cells), and emits a new BindingChunk containing only the rows
// where the predicate resolves to a truthy Value.
//
// Slow path in the MVP: per-row Bindings construction. Plan 15 can
// optimize to vectorized expression eval if profiling demands it.
class FilterOp : public Operator {
public:
    FilterOp(std::unique_ptr<Operator> input,
              const Expr*               predicate,
              StringPool&               pool,
              const std::unordered_map<uint32_t, uint32_t>& symbols);

    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override {
        return input_->output_var_names();
    }

private:
    std::unique_ptr<Operator>                             input_;
    const Expr*                                            predicate_;
    StringPool&                                            pool_;
    const std::unordered_map<uint32_t, uint32_t>&          symbols_;
};

} // namespace mora
