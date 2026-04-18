#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/eval/operator.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace mora {

// InClauseOp — vectorized implementation of the `?Var in <expr>` body clause.
//
// Two modes:
//
// Generator (unbound var + list-typed RHS):
//   For each input row, resolve `values_expr` to a List Value. For each list
//   element, emit an output row = input row with one added column binding the
//   generator var to that element.
//   Output shape: input arity + 1 (the new var column is appended last).
//
// Membership (bound var + list-typed RHS):
//   For each input row, resolve `values_expr` to a List Value. Keep the row
//   iff the var's current value is present in the list. Output shape = input.
//
// In both modes, per-row evaluation of `values_expr` via `resolve_expr`
// supports expressions that reference upstream variables (e.g.
// `?kw in keyword_list(?weapon)`).
//
// If `values_expr` does not resolve to a List, the row is skipped (matches
// the tuple-path fall-through).
class InClauseOp : public Operator {
public:
    // Factory: generator form. Adds `var_name` as a new output column.
    static std::unique_ptr<InClauseOp> build_generator(
        std::unique_ptr<Operator> input,
        StringId                   var_name,
        const Expr*                values_expr,
        StringPool&                pool,
        const std::unordered_map<uint32_t, uint32_t>& symbols);

    // Factory: membership form. Output shape = input shape.
    static std::unique_ptr<InClauseOp> build_membership(
        std::unique_ptr<Operator> input,
        StringId                   var_name,
        const Expr*                values_expr,
        StringPool&                pool,
        const std::unordered_map<uint32_t, uint32_t>& symbols);

    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override {
        return out_var_names_;
    }

private:
    enum class Mode { Generator, Membership };

    InClauseOp(std::unique_ptr<Operator> input,
                StringId                   var_name,
                const Expr*                values_expr,
                StringPool&                pool,
                const std::unordered_map<uint32_t, uint32_t>& symbols,
                Mode                       mode);

    std::unique_ptr<Operator>                             input_;
    StringId                                              var_name_;
    const Expr*                                           values_expr_;
    StringPool&                                           pool_;
    const std::unordered_map<uint32_t, uint32_t>&         symbols_;
    Mode                                                  mode_;

    std::vector<StringId>   out_var_names_;

    // Cache for the column type of the var column (generator mode only;
    // determined from the first list element we see).
    const Type* var_col_type_ = nullptr;

    // In generator mode, we may be mid-way through a list for a single
    // input row across chunk boundaries. We buffer the pending output rows
    // rather than splitting a list across chunks. (Simple: collect full
    // cross-product for each input chunk, emit resulting rows.)
    // For MVP, we collect all output rows for one input chunk at once and
    // return them as a single output chunk. This avoids complex state
    // management across next_chunk calls.
};

} // namespace mora
