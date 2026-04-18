#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace mora {

class FactDB;

// Specification of an effect arg.
// - Var:      resolved per-row from the chunk column named var_name.
// - Constant: a pre-computed literal or symbol-resolved FormID.
// - Expr:     an arbitrary expression evaluated per-row via resolve_expr.
struct EffectArgSpec {
    enum class Kind { Var, Constant, Expr };
    Kind             kind;
    StringId         var_name;  // when kind == Var
    Value            constant;  // when kind == Constant
    const mora::Expr* expr;     // when kind == Expr (pointer into AST; owned by Module)

    EffectArgSpec() : kind(Kind::Constant), var_name{}, constant{}, expr(nullptr) {}
};

// For each row of its input, EffectAppendOp appends a tuple
// (target, :field_name, value) to the appropriate skyrim/{op} relation.
// `target_spec` and `value_spec` are resolved per-row.
class EffectAppendOp {
public:
    EffectAppendOp(std::unique_ptr<Operator> input,
                   StringId                  out_relation_name,  // "skyrim/set", etc.
                   StringId                  field_keyword_id,   // interned :GoldValue, :Name, ...
                   EffectArgSpec             target_spec,
                   EffectArgSpec             value_spec,
                   StringPool&               pool,
                   const std::unordered_map<uint32_t, uint32_t>& symbols);

    // Drains the input, writing all matched rows to `db`.
    void run(FactDB& db);

private:
    std::unique_ptr<Operator>                             input_;
    StringId                                              out_relation_name_;
    StringId                                              field_kw_id_;
    EffectArgSpec                                         target_spec_;
    EffectArgSpec                                         value_spec_;
    StringPool&                                           pool_;
    const std::unordered_map<uint32_t, uint32_t>&         symbols_;
};

// For derived-fact rules (no effects), appends one tuple per input row
// to `derived_rel_name`. Each head arg is resolved per-row.
class DerivedAppendOp {
public:
    DerivedAppendOp(std::unique_ptr<Operator>   input,
                    StringId                    derived_rel_name,
                    std::vector<EffectArgSpec>  head_specs,
                    StringPool&                 pool,
                    const std::unordered_map<uint32_t, uint32_t>& symbols);

    void run(FactDB& derived_facts);

private:
    std::unique_ptr<Operator>                             input_;
    StringId                                              rel_name_;
    std::vector<EffectArgSpec>                            head_specs_;
    StringPool&                                           pool_;
    const std::unordered_map<uint32_t, uint32_t>&         symbols_;
};

} // namespace mora
