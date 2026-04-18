#include "mora/eval/op_scan.h"

#include "mora/data/vector.h"  // for kChunkSize

#include <algorithm>

namespace mora {

std::unique_ptr<ScanOp> ScanOp::build(
    const ColumnarRelation*                       relation,
    const FactPattern&                            pattern,
    StringPool&                                   pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    (void)pool;  // StringLiteral values are already interned StringIds
    auto op = std::unique_ptr<ScanOp>(new ScanOp);
    op->relation_ = relation;

    // Walk the pattern's args regardless of whether relation is null.
    // Parsing args even when relation==null ensures out_var_names_ is
    // always populated — the planner needs variable names from null-
    // relation scans to compute shared vars for JoinOp construction.
    std::unordered_map<uint32_t, size_t> first_occurrence;  // var StringId → first VarPos index
    for (size_t i = 0; i < pattern.args.size(); ++i) {
        const Expr& arg = pattern.args[i];
        if (auto const* ve = std::get_if<VariableExpr>(&arg.data)) {
            auto it = first_occurrence.find(ve->name.index);
            if (it == first_occurrence.end()) {
                first_occurrence.emplace(ve->name.index, op->var_pos_.size());
                op->var_pos_.push_back({ve->name, i});
                op->out_var_names_.push_back(ve->name);
                if (relation != nullptr) {
                    op->out_col_types_.push_back(relation->column(i).type());
                }
                // When relation==null, out_col_types_ stays short of
                // out_var_names_ — that's fine because next_chunk() returns
                // nullopt immediately (no_match_==true) and never constructs
                // a BindingChunk.
            } else {
                // Duplicate variable — equality filter between positions.
                op->eq_pos_.push_back({op->var_pos_[it->second].pattern_col, i});
            }
        } else if (auto const* se = std::get_if<SymbolExpr>(&arg.data)) {
            auto sit = symbol_formids.find(se->name.index);
            if (sit == symbol_formids.end()) {
                // Unknown symbol — scan produces nothing.
                op->no_match_ = true;
                return op;
            }
            op->const_pos_.push_back(
                {Value::make_formid(sit->second), i});
        } else if (auto const* il = std::get_if<IntLiteral>(&arg.data)) {
            op->const_pos_.push_back({Value::make_int(il->value), i});
        } else if (auto const* fl = std::get_if<FloatLiteral>(&arg.data)) {
            op->const_pos_.push_back({Value::make_float(fl->value), i});
        } else if (auto const* sl = std::get_if<StringLiteral>(&arg.data)) {
            op->const_pos_.push_back(
                {Value::make_string(sl->value), i});
        } else if (auto const* kl = std::get_if<KeywordLiteral>(&arg.data)) {
            // Mirror the tuple evaluator's match_fact_pattern logic: a
            // keyword literal may be a symbol alias (e.g. :BanditFaction
            // stored via set_symbol_formid). If found in symbol_formids,
            // treat it as a FormID constant; otherwise as an opaque keyword.
            auto sk = symbol_formids.find(kl->value.index);
            if (sk != symbol_formids.end()) {
                op->const_pos_.push_back({Value::make_formid(sk->second), i});
            } else {
                op->const_pos_.push_back({Value::make_keyword(kl->value), i});
            }
        } else {
            // EditorIdExpr, BinaryExpr, CallExpr, FieldAccessExpr — not
            // supported in MVP. The planner should have rejected this
            // rule; if we got here, caller's fault. Report no-match as
            // a safe bail.
            op->no_match_ = true;
            return op;
        }
    }

    // If relation is null, mark as no-match AFTER parsing args so that
    // out_var_names_ is populated for the planner's shared-var computation.
    if (relation == nullptr) {
        op->no_match_ = true;
    }

    return op;
}

std::optional<BindingChunk> ScanOp::next_chunk() {
    if (no_match_ || relation_ == nullptr) return std::nullopt;
    if (next_row_ >= relation_->row_count()) return std::nullopt;

    BindingChunk chunk(out_var_names_, out_col_types_);
    size_t const end = std::min(next_row_ + kChunkSize,
                                relation_->row_count());

    for (size_t row = next_row_; row < end; ++row) {
        // Filter: constant positions must match.
        bool keep = true;
        for (auto const& cp : const_pos_) {
            if (!(relation_->column(cp.pattern_col).at(row) == cp.expected)) {
                keep = false;
                break;
            }
        }
        if (!keep) continue;

        // Filter: duplicate-var positions must be equal.
        for (auto const& e : eq_pos_) {
            if (!(relation_->column(e.a).at(row) ==
                  relation_->column(e.b).at(row))) {
                keep = false;
                break;
            }
        }
        if (!keep) continue;

        // Project: pull var-positioned cells into the chunk row.
        std::vector<Value> out_row;
        out_row.reserve(var_pos_.size());
        for (auto const& vp : var_pos_) {
            out_row.push_back(relation_->column(vp.pattern_col).at(row));
        }
        chunk.append_row(out_row);
    }

    next_row_ = end;

    // If the chunk is empty, recurse to try the next window.
    if (chunk.row_count() == 0) return next_chunk();
    return chunk;
}

} // namespace mora
