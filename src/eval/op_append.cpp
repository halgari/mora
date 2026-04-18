#include "mora/eval/op_append.h"

#include "mora/eval/fact_db.h"

#include <stdexcept>

namespace mora {

namespace {

Value resolve_spec(const EffectArgSpec& spec,
                   const BindingChunk&  chunk,
                   size_t               row) {
    if (spec.kind == EffectArgSpec::Kind::Constant) return spec.constant;
    int const col = chunk.index_of(spec.var_name);
    if (col < 0) {
        throw std::runtime_error(
            "EffectAppendOp: spec references unbound variable");
    }
    return chunk.cell(row, static_cast<size_t>(col));
}

} // namespace

EffectAppendOp::EffectAppendOp(std::unique_ptr<Operator> input,
                                StringId                  out_relation_name,
                                StringId                  field_keyword_id,
                                EffectArgSpec             target_spec,
                                EffectArgSpec             value_spec)
    : input_(std::move(input))
    , out_relation_name_(out_relation_name)
    , field_kw_id_(field_keyword_id)
    , target_spec_(std::move(target_spec))
    , value_spec_(std::move(value_spec))
{}

void EffectAppendOp::run(FactDB& db) {
    Value const field_kw = Value::make_keyword(field_kw_id_);
    while (auto chunk_opt = input_->next_chunk()) {
        BindingChunk const& chunk = *chunk_opt;
        for (size_t row = 0; row < chunk.row_count(); ++row) {
            Value const target = resolve_spec(target_spec_, chunk, row);
            Value const value  = resolve_spec(value_spec_,  chunk, row);
            // target must resolve to a FormID for an effect to make sense.
            if (target.kind() != Value::Kind::FormID) continue;
            db.add_fact(out_relation_name_,
                        Tuple{target, field_kw, value});
        }
    }
}

DerivedAppendOp::DerivedAppendOp(std::unique_ptr<Operator>   input,
                                  StringId                    derived_rel_name,
                                  std::vector<EffectArgSpec>  head_specs)
    : input_(std::move(input))
    , rel_name_(derived_rel_name)
    , head_specs_(std::move(head_specs))
{}

void DerivedAppendOp::run(FactDB& derived_facts) {
    while (auto chunk_opt = input_->next_chunk()) {
        BindingChunk const& chunk = *chunk_opt;
        for (size_t row = 0; row < chunk.row_count(); ++row) {
            Tuple t;
            t.reserve(head_specs_.size());
            for (auto const& spec : head_specs_) {
                t.push_back(resolve_spec(spec, chunk, row));
            }
            derived_facts.add_fact(rel_name_, std::move(t));
        }
    }
}

} // namespace mora
