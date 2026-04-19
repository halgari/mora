#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"
#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mora {

// Scan a ColumnarRelation, filtering rows where a constant-argument
// position holds a specific value, and projecting variable-position
// columns into a BindingChunk labelled with their var names.
//
// If the same variable name appears twice in the pattern (e.g.
// `rel(?x, ?x)`), the scan applies an equality filter between those
// positions; the output chunk has a single column for that variable.
class ScanOp : public Operator {
public:
    // Factory: build a scan plan for a FactPattern against a relation.
    // `symbol_formids` supplies EditorID-to-FormID lookups used when a
    // pattern arg is a SymbolExpr.
    static std::unique_ptr<ScanOp> build(
        const ColumnarRelation*                       relation,
        const FactPattern&                            pattern,
        StringPool&                                   pool,
        const std::unordered_map<uint32_t, uint32_t>& symbol_formids);

    std::optional<BindingChunk> next_chunk() override;

    // Exposed for planner sanity: the output variable names, in column
    // order. Returns empty if build() produced a no-match plan (e.g.
    // a symbol pattern with an unknown EditorID — scan yields nothing).
    const std::vector<StringId>& output_var_names() const override { return out_var_names_; }

private:
    struct VarPos   { StringId name; size_t pattern_col; };
    struct ConstPos { Value    expected; size_t pattern_col; };
    struct EqPos    { size_t a; size_t b; };  // equality filter between positions

    ScanOp() = default;

    const ColumnarRelation*  relation_ = nullptr;
    std::vector<VarPos>      var_pos_;        // positions mapped to vars (dedup'd)
    std::vector<ConstPos>    const_pos_;      // constant-valued positions
    std::vector<EqPos>       eq_pos_;         // duplicate-var equality filters
    std::vector<StringId>    out_var_names_;
    std::vector<const Type*> out_col_types_;

    size_t next_row_ = 0;
    bool   no_match_ = false;  // fast exit when build() detected impossibility
};

} // namespace mora
