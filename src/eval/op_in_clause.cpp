#include "mora/eval/op_in_clause.h"

#include "mora/core/type.h"
#include "mora/eval/expr_eval.h"

#include <cassert>

namespace mora {

// ---------------------------------------------------------------------------
// Constructor (private)
// ---------------------------------------------------------------------------

InClauseOp::InClauseOp(std::unique_ptr<Operator> input,
                         StringId                   var_name,
                         const Expr*                values_expr,
                         StringPool&                pool,
                         const std::unordered_map<uint32_t, uint32_t>& symbols,
                         Mode                       mode)
    : input_(std::move(input))
    , var_name_(var_name)
    , values_expr_(values_expr)
    , pool_(pool)
    , symbols_(symbols)
    , mode_(mode)
{
    assert(input_ != nullptr && "InClauseOp: input must not be null");
    // values_expr_ may be null for MembershipSet mode (which uses membership_exprs_).
    assert((values_expr_ != nullptr || mode_ == Mode::MembershipSet) &&
           "InClauseOp: values_expr must not be null except in MembershipSet mode");

    // Output var names:
    // - Membership: same as input (var already present in input).
    // - Generator:  input names + the new var column appended.
    out_var_names_ = input_->output_var_names();
    if (mode_ == Mode::Generator) {
        // Check the var is not already present (generator only adds new vars).
        for (auto const& n : out_var_names_) {
            (void)n;
            assert(n.index != var_name_.index &&
                   "InClauseOp generator: var already bound — use membership mode");
        }
        out_var_names_.push_back(var_name_);
    }
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

std::unique_ptr<InClauseOp> InClauseOp::build_generator(
    std::unique_ptr<Operator> input,
    StringId                   var_name,
    const Expr*                values_expr,
    StringPool&                pool,
    const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    return std::unique_ptr<InClauseOp>(new InClauseOp(
        std::move(input), var_name, values_expr, pool, symbols, Mode::Generator));
}

std::unique_ptr<InClauseOp> InClauseOp::build_membership(
    std::unique_ptr<Operator> input,
    StringId                   var_name,
    const Expr*                values_expr,
    StringPool&                pool,
    const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    return std::unique_ptr<InClauseOp>(new InClauseOp(
        std::move(input), var_name, values_expr, pool, symbols, Mode::Membership));
}

std::unique_ptr<InClauseOp> InClauseOp::build_membership_set(
    std::unique_ptr<Operator>            input,
    StringId                              var_name,
    std::vector<const Expr*>              value_exprs,
    StringPool&                           pool,
    const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    // Use a sentinel values_expr (nullptr) for MembershipSet mode — next_chunk
    // does not use values_expr_ in this mode.
    auto op = std::unique_ptr<InClauseOp>(new InClauseOp(
        std::move(input), var_name, nullptr, pool, symbols, Mode::MembershipSet));

    // Pre-resolve constant expressions (literals, symbols) at build time.
    // Dynamic exprs are stored for per-row evaluation.
    Bindings empty_bindings;
    for (const Expr* e : value_exprs) {
        if (!e) continue;
        // Try to evaluate with empty bindings — succeeds for constants.
        Value v = resolve_expr(*e, empty_bindings, pool, symbols);
        if (!v.is_var()) {
            op->membership_consts_.push_back(v);
        } else {
            op->membership_exprs_.push_back(e);
        }
    }
    return op;
}

// ---------------------------------------------------------------------------
// next_chunk
// ---------------------------------------------------------------------------

std::optional<BindingChunk> InClauseOp::next_chunk() {
    while (true) {
        auto in_opt = input_->next_chunk();
        if (!in_opt) return std::nullopt;
        const BindingChunk& in = *in_opt;

        if (mode_ == Mode::Generator) {
            // Generator: for each input row × each list element → one output row.
            // Output shape: input columns + 1 (the new var column).

            // Collect all output rows for this chunk, then emit as one chunk.
            // We defer building the BindingChunk until we know the element type
            // (determined from the first list element we encounter).
            struct PendingRow {
                std::vector<Value> input_cells;
                Value              elem;
            };
            std::vector<PendingRow> pending;

            for (size_t r = 0; r < in.row_count(); ++r) {
                // Build bindings for this input row.
                Bindings b;
                b.reserve(in.arity());
                for (size_t c = 0; c < in.arity(); ++c) {
                    b[in.name_at(c).index] = in.cell(r, c);
                }

                Value rhs = resolve_expr(*values_expr_, b, pool_, symbols_);
                if (rhs.kind() != Value::Kind::List) continue;

                // Collect the input row's cells once (reused for each element).
                std::vector<Value> row_cells;
                row_cells.reserve(in.arity());
                for (size_t c = 0; c < in.arity(); ++c) {
                    row_cells.push_back(in.cell(r, c));
                }

                for (const Value& elem : rhs.as_list()) {
                    pending.push_back({row_cells, elem});
                }
            }

            if (pending.empty()) continue;  // no output rows from this chunk — pull next

            // Determine output types: input column types + any() for the new var.
            // We use types::any() for the generator var column because list
            // elements may be heterogeneous (FormID, Keyword, etc.).
            if (var_col_type_ == nullptr) {
                var_col_type_ = types::any();
            }

            std::vector<const Type*> out_types;
            out_types.reserve(in.arity() + 1);
            for (size_t c = 0; c < in.arity(); ++c) {
                out_types.push_back(in.column(c).type());
            }
            out_types.push_back(var_col_type_);

            BindingChunk out(out_var_names_, out_types);
            for (auto& pr : pending) {
                pr.input_cells.push_back(pr.elem);
                out.append_row(pr.input_cells);
            }
            return out;

        } else if (mode_ == Mode::MembershipSet) {
            // MembershipSet: multi-value literal set `?x in [:A, :B, ...]`.
            // Keep rows where the var's value is in the pre-resolved constant set
            // or matches any dynamic expression.

            if (membership_consts_.empty() && membership_exprs_.empty()) {
                // Empty set — no rows pass. Pull next chunk.
                continue;
            }

            int var_col = in.index_of(var_name_);
            assert(var_col >= 0 && "InClauseOp membership_set: var not found in input chunk");

            std::vector<const Type*> out_types;
            out_types.reserve(in.arity());
            for (size_t c = 0; c < in.arity(); ++c) {
                out_types.push_back(in.column(c).type());
            }
            BindingChunk out(in.names(), out_types);

            for (size_t r = 0; r < in.row_count(); ++r) {
                Value const var_val = in.cell(r, static_cast<size_t>(var_col));

                // Check pre-resolved constants.
                bool pass = false;
                for (auto const& cv : membership_consts_) {
                    if (var_val == cv) { pass = true; break; }
                }

                if (!pass && !membership_exprs_.empty()) {
                    // Build per-row bindings for dynamic expressions.
                    Bindings b;
                    b.reserve(in.arity());
                    for (size_t c = 0; c < in.arity(); ++c) {
                        b[in.name_at(c).index] = in.cell(r, c);
                    }
                    for (const Expr* e : membership_exprs_) {
                        Value v = resolve_expr(*e, b, pool_, symbols_);
                        if (var_val == v) { pass = true; break; }
                    }
                }

                if (!pass) continue;

                std::vector<Value> row_cells;
                row_cells.reserve(in.arity());
                for (size_t c = 0; c < in.arity(); ++c) {
                    row_cells.push_back(in.cell(r, c));
                }
                out.append_row(row_cells);
            }

            if (out.row_count() > 0) return out;
            // All rows filtered — pull next chunk.

        } else {
            // Membership: keep rows where the var's current value is in the list.

            // Find which column holds the var.
            int var_col = in.index_of(var_name_);
            assert(var_col >= 0 && "InClauseOp membership: var not found in input chunk");

            // Build output with same shape.
            std::vector<const Type*> out_types;
            out_types.reserve(in.arity());
            for (size_t c = 0; c < in.arity(); ++c) {
                out_types.push_back(in.column(c).type());
            }
            BindingChunk out(in.names(), out_types);

            for (size_t r = 0; r < in.row_count(); ++r) {
                // Build bindings for this row.
                Bindings b;
                b.reserve(in.arity());
                for (size_t c = 0; c < in.arity(); ++c) {
                    b[in.name_at(c).index] = in.cell(r, c);
                }

                Value rhs = resolve_expr(*values_expr_, b, pool_, symbols_);
                if (rhs.kind() != Value::Kind::List) continue;

                Value const var_val = in.cell(r, static_cast<size_t>(var_col));
                if (!rhs.list_contains(var_val)) continue;

                // Row passes: include it.
                std::vector<Value> row_cells;
                row_cells.reserve(in.arity());
                for (size_t c = 0; c < in.arity(); ++c) {
                    row_cells.push_back(in.cell(r, c));
                }
                out.append_row(row_cells);
            }

            if (out.row_count() > 0) return out;
            // All rows filtered — pull next chunk.
        }
    }
}

} // namespace mora
