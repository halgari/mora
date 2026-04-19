#include "mora/eval/op_filter.h"

#include "mora/eval/expr_eval.h"

namespace mora {

FilterOp::FilterOp(std::unique_ptr<Operator> input,
                    const Expr*               predicate,
                    StringPool&               pool,
                    const std::unordered_map<uint32_t, uint32_t>& symbols)
    : input_(std::move(input))
    , predicate_(predicate)
    , pool_(pool)
    , symbols_(symbols)
{}

std::optional<BindingChunk> FilterOp::next_chunk() {
    while (auto in_chunk = input_->next_chunk()) {
        BindingChunk& chunk = *in_chunk;

        // Build a new chunk with the same column shape.
        std::vector<const Type*> out_types;
        out_types.reserve(chunk.arity());
        for (size_t i = 0; i < chunk.arity(); ++i) {
            out_types.push_back(chunk.column(i).type());
        }
        BindingChunk out(chunk.names(), out_types);

        for (size_t row = 0; row < chunk.row_count(); ++row) {
            // Build per-row Bindings map.
            Bindings b;
            b.reserve(chunk.arity());
            for (size_t col = 0; col < chunk.arity(); ++col) {
                b[chunk.name_at(col).index] = chunk.cell(row, col);
            }
            if (evaluate_guard(*predicate_, b, pool_, symbols_)) {
                std::vector<Value> row_values;
                row_values.reserve(chunk.arity());
                for (size_t col = 0; col < chunk.arity(); ++col) {
                    row_values.push_back(chunk.cell(row, col));
                }
                out.append_row(row_values);
            }
        }

        if (out.row_count() > 0) return out;
        // All rows filtered out — pull next chunk.
    }
    return std::nullopt;
}

} // namespace mora
