#include "mora/eval/op_append.h"

#include "mora/eval/expr_eval.h"
#include "mora/eval/fact_db.h"

#include <stdexcept>

namespace mora {

namespace {

Value resolve_spec(const EffectArgSpec&                          spec,
                   const BindingChunk&                           chunk,
                   size_t                                        row,
                   StringPool&                                   pool,
                   const std::unordered_map<uint32_t, uint32_t>& symbols) {
    switch (spec.kind) {
        case EffectArgSpec::Kind::Constant:
            return spec.constant;
        case EffectArgSpec::Kind::Var: {
            int const col = chunk.index_of(spec.var_name);
            if (col < 0) {
                throw std::runtime_error(
                    "DerivedAppendOp: spec references unbound variable");
            }
            return chunk.cell(row, static_cast<size_t>(col));
        }
        case EffectArgSpec::Kind::Expr: {
            // Build per-row Bindings from the chunk.
            Bindings b;
            b.reserve(chunk.arity());
            for (size_t c = 0; c < chunk.arity(); ++c) {
                b[chunk.name_at(c).index] = chunk.cell(row, c);
            }
            return resolve_expr(*spec.expr, b, pool, symbols);
        }
    }
    return Value{};
}

} // namespace

DerivedAppendOp::DerivedAppendOp(std::unique_ptr<Operator>   input,
                                  StringId                    derived_rel_name,
                                  std::vector<EffectArgSpec>  head_specs,
                                  StringPool&                 pool,
                                  const std::unordered_map<uint32_t, uint32_t>& symbols)
    : input_(std::move(input))
    , rel_name_(derived_rel_name)
    , head_specs_(std::move(head_specs))
    , pool_(pool)
    , symbols_(symbols)
{}

void DerivedAppendOp::run(FactDB& derived_facts) {
    while (auto chunk_opt = input_->next_chunk()) {
        BindingChunk const& chunk = *chunk_opt;
        for (size_t row = 0; row < chunk.row_count(); ++row) {
            Tuple t;
            t.reserve(head_specs_.size());
            for (auto const& spec : head_specs_) {
                t.push_back(resolve_spec(spec, chunk, row, pool_, symbols_));
            }
            derived_facts.add_fact(rel_name_, std::move(t));
        }
    }
}

} // namespace mora
