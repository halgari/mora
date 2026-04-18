# Plan 13 — Vectorized evaluator MVP (positive-conjunction rules)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a vectorized query engine — Scan/Join/EffectAppend/DerivedAppend operators over `BindingChunk`s — and route **positive-conjunction rules** through it. Rules with guards, negated patterns, InClauses, or conditional effects keep flowing through the existing tuple-based `match_clauses` path. After Plan 13, every single-body-clause and every pure positive-conjunction multi-clause rule executes vectorized; everything else falls back. Plans 14–16 expand coverage, add seminaive fixpoint, delete the fallback, and drop verb keywords.

**Architecture:** Pull-model operators. `Operator::next_chunk() → optional<BindingChunk>`. A `BindingChunk` is N rows × K bound variables, where each column is a typed `Column` (Plan 10) labelled with a `StringId` variable name. `ScanOp(relation, args)` reads rows from a `ColumnarRelation` (Plan 11), filters constant-argument positions, and projects the remaining variable positions into a binding chunk. `JoinOp(left, right, shared_vars)` hash-joins the two streams on shared variable columns. `EffectAppendOp` consumes the final binding chunk and writes `(target_formid, field_keyword, value)` rows into `skyrim/{set,add,remove,multiply}`. `DerivedAppendOp` writes rule-head rows to `derived_facts_`. A `Planner` translates an AST `Rule` into an operator tree if the rule is supported; if not, the evaluator runs the existing tuple path.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `319bb0b` (HEAD after Plan 12, 28 commits above master)

**Scope note.** Plan 13 is MVP — just enough of the vectorized pipeline to prove the design on real rules. No guards, no negation, no InClause, no conditional effects. Plan 14 adds those. Plan 15 adds seminaive + deletes the tuple evaluator. Plan 16 drops verb keywords.

**Supported rule shapes (M1 + M2):**
- Body clauses: only positive `FactPattern` (any number, including zero — though zero-clause rules with effects are degenerate). No `GuardClause`, `InClause`, `ConditionalEffect`.
- Effects: any mix of unconditional `Effect`s. Conditional effects (the ones with an embedded `guard`) force fallback.
- Effect args: must be `VariableExpr`, `IntLiteral`, `FloatLiteral`, `StringLiteral`, `KeywordLiteral`, or `SymbolExpr`. `BinaryExpr`, `CallExpr`, `FieldAccessExpr`, `EditorIdExpr` force fallback (these require scalar expression evaluation Plan 14 will add).
- Derived-fact rules (no effects): `head_args` must all be `VariableExpr` bound by the body. Anything else forces fallback.
- LeveledEntries packing (from `apply_effects`'s special branch): forces fallback. Re-home to a vectorized operator in a later plan.

Everything else keeps working via `match_clauses`. The goal: every CLI compile of `test_data/minimal` / `test_data/example.mora` produces **byte-identical parquet output** to Plan 12 — the vectorized path is a pure refactor, never a behavior change.

---

## Milestone 1 — Operator framework + single-clause rules

Stand up the infrastructure and wire it to rules with exactly one positive FactPattern body clause (no joins yet).

### Task 1.1: `BindingChunk` — the pipeline's payload

**Files:**
- Create: `include/mora/eval/binding_chunk.h`
- Create: `src/eval/binding_chunk.cpp`

A `BindingChunk` is a fixed-arity row bag where each column is a `Column` (Plan 10) labelled with a variable name. Pipelines pass these between operators. Columns inside a chunk hold up to one `Vector` chunk (max `kChunkSize` rows) — the pipeline respects the 2048-row budget by construction.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/column.h"
#include "mora/data/value.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mora {

// A BindingChunk is a slice of N rows × K bound variables. Each column
// carries the `Type*` and typed values of one variable. The `names_`
// vector positions each column with a variable identity (`StringId`).
// `index_of(name)` is the caller's way to look up which column holds
// a given variable.
//
// A BindingChunk is append-only during construction — operators build
// one in a staging phase (append rows until `kChunkSize` or source
// exhaustion), then emit it downstream.
class BindingChunk {
public:
    BindingChunk(std::vector<StringId>       var_names,
                 std::vector<const Type*>    col_types);

    // Arity (number of bound variables in this chunk).
    size_t arity()     const { return columns_.size(); }
    size_t row_count() const;

    // Access a column by position. Downstream ops downcast the chunk's
    // Vector to a typed Vector when they need typed bulk access.
    Column&       column(size_t i)       { return *columns_[i]; }
    const Column& column(size_t i) const { return *columns_[i]; }

    // Lookup a column index by variable name. Returns -1 if absent.
    int index_of(StringId var_name) const;

    // Variable name at position i.
    StringId name_at(size_t i) const { return names_[i]; }
    const std::vector<StringId>& names() const { return names_; }

    // Append one row. The caller provides a Value per column, in
    // position order. Kind must match each column's hint (enforced by
    // Column::append).
    void append_row(const std::vector<Value>& row);

    // Build a Value representing the cell at (row, col). Used by
    // operators that emit individual rows to a downstream sink.
    Value cell(size_t row, size_t col) const;

private:
    std::vector<StringId>                  names_;
    std::vector<std::unique_ptr<Column>>    columns_;
    // For quick index_of; populated by ctor.
    std::unordered_map<uint32_t, size_t>    by_name_;
};

} // namespace mora
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/eval/binding_chunk.cpp
#include "mora/eval/binding_chunk.h"

#include <stdexcept>

namespace mora {

BindingChunk::BindingChunk(std::vector<StringId>    var_names,
                             std::vector<const Type*> col_types)
    : names_(std::move(var_names))
{
    if (names_.size() != col_types.size()) {
        throw std::runtime_error(
            "BindingChunk: var_names and col_types arity mismatch");
    }
    columns_.reserve(col_types.size());
    for (size_t i = 0; i < col_types.size(); ++i) {
        columns_.push_back(std::make_unique<Column>(col_types[i]));
        by_name_.emplace(names_[i].index, i);
    }
}

size_t BindingChunk::row_count() const {
    return columns_.empty() ? 0 : columns_.front()->row_count();
}

int BindingChunk::index_of(StringId var_name) const {
    auto it = by_name_.find(var_name.index);
    if (it == by_name_.end()) return -1;
    return static_cast<int>(it->second);
}

void BindingChunk::append_row(const std::vector<Value>& row) {
    if (row.size() != columns_.size()) {
        throw std::runtime_error(
            "BindingChunk::append_row: row arity mismatch");
    }
    for (size_t i = 0; i < row.size(); ++i) {
        columns_[i]->append(row[i]);
    }
}

Value BindingChunk::cell(size_t row, size_t col) const {
    return columns_[col]->at(row);
}

} // namespace mora
```

- [ ] **Step 3: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

Expected: compiles. Nothing uses it yet.

### Task 1.2: `Operator` base class + `ScanOp`

**Files:**
- Create: `include/mora/eval/operator.h`
- Create: `src/eval/operator.cpp`
- Create: `include/mora/eval/op_scan.h`
- Create: `src/eval/op_scan.cpp`

- [ ] **Step 1: Write `operator.h`**

```cpp
#pragma once

#include "mora/eval/binding_chunk.h"

#include <memory>
#include <optional>

namespace mora {

// Pull-model iterator. Each call to next_chunk() produces the next
// BindingChunk (up to kChunkSize rows) or nullopt when the stream is
// exhausted.
class Operator {
public:
    virtual ~Operator()                        = default;
    virtual std::optional<BindingChunk> next_chunk() = 0;
};

} // namespace mora
```

- [ ] **Step 2: Write `op_scan.h`**

```cpp
#pragma once

#include "mora/ast/ast.h"              // FactPattern + Expr
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"
#include "mora/data/columnar_relation.h"

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
        const ColumnarRelation*                     relation,
        const FactPattern&                          pattern,
        StringPool&                                 pool,
        const std::unordered_map<uint32_t, uint32_t>& symbol_formids);

    std::optional<BindingChunk> next_chunk() override;

    // Exposed for planner sanity: the output variable names, in column
    // order. Returns empty if build() produced a no-match plan (e.g.
    // a symbol pattern with an unknown EditorID — scan yields nothing).
    const std::vector<StringId>& output_var_names() const { return out_var_names_; }

private:
    struct VarPos     { StringId name; size_t pattern_col; };
    struct ConstPos   { Value    expected; size_t pattern_col; };
    struct EqPos      { size_t a; size_t b; };  // equality filter between positions

    ScanOp() = default;

    const ColumnarRelation*                            relation_ = nullptr;
    std::vector<VarPos>                                 var_pos_;   // positions mapped to vars (dedup'd)
    std::vector<ConstPos>                               const_pos_; // constant-valued positions
    std::vector<EqPos>                                  eq_pos_;    // duplicate-var equality filters
    std::vector<StringId>                               out_var_names_;
    std::vector<const Type*>                            out_col_types_;

    size_t next_row_ = 0;
    bool   no_match_ = false;   // fast exit when build() detected impossibility
};

} // namespace mora
```

- [ ] **Step 3: Write `op_scan.cpp`**

```cpp
#include "mora/eval/op_scan.h"

#include <algorithm>

namespace mora {

std::unique_ptr<ScanOp> ScanOp::build(
    const ColumnarRelation*                     relation,
    const FactPattern&                          pattern,
    StringPool&                                 pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    auto op = std::unique_ptr<ScanOp>(new ScanOp);
    op->relation_ = relation;

    if (relation == nullptr) {
        op->no_match_ = true;
        return op;
    }

    // Walk the pattern's args. Vars → VarPos; literals/symbols → ConstPos.
    std::unordered_map<uint32_t, size_t> first_occurrence;  // var StringId → first VarPos index
    for (size_t i = 0; i < pattern.args.size(); ++i) {
        const Expr& arg = pattern.args[i];
        if (auto const* ve = std::get_if<VariableExpr>(&arg.data)) {
            auto it = first_occurrence.find(ve->name.index);
            if (it == first_occurrence.end()) {
                first_occurrence.emplace(ve->name.index, op->var_pos_.size());
                op->var_pos_.push_back({ve->name, i});
                op->out_var_names_.push_back(ve->name);
                op->out_col_types_.push_back(relation->column(i).type());
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
                {Value::make_string(pool.intern(sl->value)), i});
        } else if (auto const* kl = std::get_if<KeywordLiteral>(&arg.data)) {
            op->const_pos_.push_back({Value::make_keyword(kl->value), i});
        } else {
            // EditorIdExpr, BinaryExpr, CallExpr, FieldAccessExpr — not
            // supported in MVP. The planner should have rejected this
            // rule; if we got here, caller's fault. Report no-match as
            // a safe bail.
            op->no_match_ = true;
            return op;
        }
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

    // Emit the chunk even if it's empty — downstream may still process
    // subsequent chunks. Actually, if empty, skip and recurse.
    if (chunk.row_count() == 0) return next_chunk();
    return chunk;
}

} // namespace mora
```

- [ ] **Step 4: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

Expected: compiles. Still no consumer.

### Task 1.3: `EffectAppendOp` and `DerivedAppendOp` — sink ops

**Files:**
- Create: `include/mora/eval/op_append.h`
- Create: `src/eval/op_append.cpp`

These consume a binding chunk stream and write results to FactDB. EffectAppendOp emits `(target_formid, field_keyword, value)` rows to an effect relation. DerivedAppendOp writes the rule head's var values to the derived-rule relation.

- [ ] **Step 1: Write `op_append.h`**

```cpp
#pragma once

#include "mora/core/string_pool.h"
#include "mora/eval/binding_chunk.h"
#include "mora/eval/field_types.h"
#include "mora/eval/operator.h"

#include <memory>
#include <variant>
#include <vector>

namespace mora {

class FactDB;

// Specification of an effect arg. Either a binding variable (resolved
// per-row from the chunk) or a constant literal.
struct EffectArgSpec {
    enum class Kind { Var, Constant };
    Kind        kind;
    StringId    var_name;   // when kind == Var
    Value       constant;   // when kind == Constant
};

// For each row of its input, EffectAppendOp appends a tuple
// (target, :field_name, value) to the appropriate skyrim/{op} relation.
// `target_spec` and `value_spec` are resolved per-row.
class EffectAppendOp {
public:
    EffectAppendOp(std::unique_ptr<Operator> input,
                    StringId                 out_relation_name,   // "skyrim/set", etc.
                    StringId                 field_keyword_id,    // interned :GoldValue, :Name, ...
                    EffectArgSpec            target_spec,
                    EffectArgSpec            value_spec);

    // Drains the input, writing all matched rows to `db`.
    void run(FactDB& db);

private:
    std::unique_ptr<Operator> input_;
    StringId                  out_relation_name_;
    StringId                  field_kw_id_;
    EffectArgSpec             target_spec_;
    EffectArgSpec             value_spec_;
};

// For derived-fact rules (no effects), appends one tuple per input row
// to `derived_rel_name`. Each head arg is resolved per-row.
class DerivedAppendOp {
public:
    DerivedAppendOp(std::unique_ptr<Operator>    input,
                     StringId                    derived_rel_name,
                     std::vector<EffectArgSpec>  head_specs);

    void run(FactDB& derived_facts);

private:
    std::unique_ptr<Operator>   input_;
    StringId                    rel_name_;
    std::vector<EffectArgSpec>  head_specs_;
};

} // namespace mora
```

- [ ] **Step 2: Write `op_append.cpp`**

```cpp
#include "mora/eval/op_append.h"

#include "mora/eval/fact_db.h"

#include <stdexcept>

namespace mora {

static Value resolve_spec(const EffectArgSpec& spec,
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

EffectAppendOp::EffectAppendOp(std::unique_ptr<Operator> input,
                                 StringId                 out_relation_name,
                                 StringId                 field_keyword_id,
                                 EffectArgSpec            target_spec,
                                 EffectArgSpec            value_spec)
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

DerivedAppendOp::DerivedAppendOp(std::unique_ptr<Operator>    input,
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
```

- [ ] **Step 3: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

Expected: compiles.

### Task 1.4: Planner + wire it into the evaluator (single-clause only)

**Files:**
- Create: `include/mora/eval/rule_planner.h`
- Create: `src/eval/rule_planner.cpp`
- Modify: `include/mora/eval/evaluator.h`
- Modify: `src/eval/evaluator.cpp`

- [ ] **Step 1: Write `rule_planner.h`**

```cpp
#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/data/action_names.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/operator.h"
#include "mora/eval/op_append.h"

#include <memory>
#include <optional>
#include <unordered_map>

namespace mora {

// Outcome of attempting to build a vectorized plan for a rule.
// `run` is present iff the rule was supported — the planner already
// constructed the full operator tree; caller just invokes it.
struct RulePlan {
    // Either `effect_op` or `derived_op` is populated (never both).
    std::unique_ptr<EffectAppendOp>              effect_op;
    std::unique_ptr<DerivedAppendOp>             derived_op;

    // Multiple effects per rule produce multiple EffectAppendOps; they
    // share the same body operator tree via run-once / re-scan semantics.
    // For the simplest MVP we only support ONE effect; rules with
    // multiple effects force fallback.
};

// Returns RulePlan if the rule is supported in MVP; otherwise nullopt
// (caller falls back to tuple evaluator).
std::optional<RulePlan> plan_rule(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids);

} // namespace mora
```

- [ ] **Step 2: Write `rule_planner.cpp`** — M1 version (single positive FactPattern, optional effects, no guards/InClause/negation/conditional)

```cpp
#include "mora/eval/rule_planner.h"

#include "mora/eval/op_scan.h"

#include <string>

namespace mora {
namespace {

// Predicates on AST shapes ----------------------------------------------

bool is_simple_arg_expr(const Expr& e) {
    return std::holds_alternative<VariableExpr>(e.data)
        || std::holds_alternative<IntLiteral>(e.data)
        || std::holds_alternative<FloatLiteral>(e.data)
        || std::holds_alternative<StringLiteral>(e.data)
        || std::holds_alternative<KeywordLiteral>(e.data)
        || std::holds_alternative<SymbolExpr>(e.data);
}

bool body_is_positive_conjunction(const Rule& rule) {
    for (auto const& clause : rule.body) {
        bool ok = std::visit([](auto const& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) {
                return !c.negated;   // positive only in MVP
            } else {
                return false;        // GuardClause / InClause / Effect / ConditionalEffect → no
            }
        }, clause.data);
        if (!ok) return false;
    }
    return true;
}

// EffectArgSpec from an Expr, using the same allowlist as is_simple_arg_expr.
EffectArgSpec spec_from_expr(const Expr& e, StringPool& pool,
                               const std::unordered_map<uint32_t, uint32_t>& symbols) {
    EffectArgSpec s{};
    if (auto const* ve = std::get_if<VariableExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Var;
        s.var_name = ve->name;
        return s;
    }
    s.kind = EffectArgSpec::Kind::Constant;
    if (auto const* il = std::get_if<IntLiteral>(&e.data))   s.constant = Value::make_int(il->value);
    else if (auto const* fl = std::get_if<FloatLiteral>(&e.data)) s.constant = Value::make_float(fl->value);
    else if (auto const* sl = std::get_if<StringLiteral>(&e.data)) s.constant = Value::make_string(pool.intern(sl->value));
    else if (auto const* kl = std::get_if<KeywordLiteral>(&e.data)) s.constant = Value::make_keyword(kl->value);
    else if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        auto it = symbols.find(se->name.index);
        s.constant = it == symbols.end()
            ? Value::make_var()  // unresolved — op will bail
            : Value::make_formid(it->second);
    }
    return s;
}

// Map Effect → (field_id, op). This duplicates Evaluator::action_to_field's
// legacy-name assembly since that method is private. Once Plan 14 unifies
// the effect dispatch we can consolidate.
// For MVP, just call out to action_to_field via the evaluator. Instead,
// re-implement here using `model::action_to_field` if it exists, or
// the mapping logic from evaluator.cpp lines 632–678.
// For simplicity, this MVP copies the dispatch logic inline via the
// `reconstruct_legacy_name` helper.

}  // namespace

std::optional<RulePlan> plan_rule(
    const Rule&                                  rule,
    const FactDB&                                input_db,
    const FactDB&                                derived_facts,
    StringPool&                                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    // M1 restriction: exactly one body FactPattern (positive).
    if (rule.body.size() != 1) return std::nullopt;
    if (!body_is_positive_conjunction(rule)) return std::nullopt;

    auto const& fp = std::get<FactPattern>(rule.body[0].data);

    // All pattern args must be "simple" exprs.
    for (auto const& a : fp.args) {
        if (!is_simple_arg_expr(a)) return std::nullopt;
    }

    // Conditional effects force fallback.
    if (!rule.conditional_effects.empty()) return std::nullopt;

    // Build a ScanOp against the input FactDB's relation. If the pattern
    // is namespace-qualified, use the input DB; otherwise check derived
    // facts first (mimics merged_query semantics at clause level).
    StringId const rel_name = fp.name;
    const ColumnarRelation* rel = input_db.get_relation_columnar(rel_name);
    if (rel == nullptr) {
        rel = derived_facts.get_relation_columnar(rel_name);
    }

    auto scan = ScanOp::build(rel, fp, pool, symbol_formids);

    // Effects branch -----------------------------------------------------
    if (!rule.effects.empty()) {
        // MVP: exactly one effect. Multiple effects → fallback.
        if (rule.effects.size() > 1) return std::nullopt;
        const Effect& eff = rule.effects[0];
        if (eff.args.size() != 2) return std::nullopt;          // set/add/etc. take (target, value)
        if (!is_simple_arg_expr(eff.args[0]) || !is_simple_arg_expr(eff.args[1]))
            return std::nullopt;

        // Resolve the verb + name to a (field_id, field_op) pair, then
        // pick the corresponding skyrim/{set,add,remove,multiply} relation
        // and the interned field keyword. This reuses the Evaluator's
        // private `action_to_field` dispatch by re-implementing it locally
        // here — see note in the anonymous namespace above.
        //
        // For MVP, we only handle `set_*` verbs (FieldOp::Set). Anything
        // else falls back. This covers the bulk of Skyrim rules; Plan 14
        // expands verb coverage.
        std::string legacy;
        switch (eff.verb) {
            case VerbKind::Set: legacy = "set_"; break;
            default: return std::nullopt;  // Add/Mul/Sub/Remove → fallback
        }
        legacy += pool.get(eff.name);
        StringId const action_id = pool.intern(legacy);

        // We need the (field, op) mapping. Rather than duplicate the
        // lookup tables here, defer to a future plan. For M1, just check
        // that action exists in the Skyrim tables and build the field
        // keyword name by dropping the "set_" prefix.
        // Use `eff.name` directly as the keyword name, capitalized per
        // action_names convention — e.g. "gold_value" → "GoldValue".
        // Again, a future plan consolidates this. For MVP we call into
        // a local helper that forwards to `action_to_field` via a weak
        // ABI; actually the cleanest route is to expose action_to_field
        // on Evaluator publicly. See Task 1.4 Step 3 — the evaluator
        // hands the planner a functor for this.
        //
        // (The implementer picks the approach that works; the plan
        // specifies the required behavior: reject rules whose effect
        // verb+name don't map to a known (FieldId, FieldOp); for
        // supported ones, pick the right output relation name and build
        // the field keyword.)
        //
        // For this file, we assume the caller supplied a callable
        // `effect_dispatch(eff) → optional<(relation_name, field_kw)>`
        // — but that requires a bigger planner API. Simpler: do the
        // dispatch inline here once action_to_field becomes accessible
        // via the Evaluator. The implementer should expose it as a
        // free function `action_to_field(StringId)` in a shared header
        // (e.g. move the logic from evaluator.cpp into
        // src/data/action_names.cpp as a free function that can be
        // reused). Task 1.4 Step 3 below handles that.
        return std::nullopt;  // Placeholder — implementer completes after Step 3.
    }

    // Derived rule branch -----------------------------------------------
    if (rule.effects.empty() && rule.conditional_effects.empty()) {
        // All head args must be VariableExpr referring to vars bound by
        // the body scan.
        std::vector<EffectArgSpec> head_specs;
        head_specs.reserve(rule.head_args.size());
        for (auto const& ha : rule.head_args) {
            if (!std::holds_alternative<VariableExpr>(ha.data))
                return std::nullopt;
            head_specs.push_back(spec_from_expr(ha, pool, symbol_formids));
        }
        RulePlan plan;
        plan.derived_op = std::make_unique<DerivedAppendOp>(
            std::move(scan), rule.name, std::move(head_specs));
        return plan;
    }

    return std::nullopt;
}

} // namespace mora
```

The planner's Effects branch references a "local `action_to_field` dispatcher" that doesn't yet exist as a shared symbol. Handle in Step 3.

- [ ] **Step 3: Expose `action_to_field` as a free function**

**File:** `include/mora/data/action_names.h` (modify to add a free function)
**File:** `src/data/action_names.cpp` (new — hosts the implementation that today lives inside `Evaluator::action_to_field` at `src/eval/evaluator.cpp:632-678`)

Move the logic. Keep `Evaluator::action_to_field` as a thin wrapper that delegates. Free function signature:

```cpp
// In include/mora/data/action_names.h
std::pair<FieldId, FieldOp> action_to_field(StringId action_id,
                                              const StringPool& pool);
```

The M1 planner uses this. Then complete the Effects branch:

```cpp
// After building `action_id` in the planner:
auto [field, op] = action_to_field(action_id, pool);
if (field == FieldId::Invalid) return std::nullopt;  // unknown action

// Only Set verbs in M1.
if (op != FieldOp::Set) return std::nullopt;

StringId const out_rel = pool.intern("skyrim/set");
StringId const field_kw_id = pool.intern(field_id_name(field));

EffectArgSpec target_spec = spec_from_expr(eff.args[0], pool, symbol_formids);
EffectArgSpec value_spec  = spec_from_expr(eff.args[1], pool, symbol_formids);

RulePlan plan;
plan.effect_op = std::make_unique<EffectAppendOp>(
    std::move(scan), out_rel, field_kw_id,
    std::move(target_spec), std::move(value_spec));
return plan;
```

Add a `FieldId::Invalid` sentinel if it doesn't exist (check `include/mora/eval/field_types.h`). If it does exist, use it; if not, add it as the terminal enum value (numerically largest or a separate `Invalid = 0xFF`).

- [ ] **Step 4: Wire the planner into `Evaluator::evaluate_rule`**

**File:** `src/eval/evaluator.cpp`

In `Evaluator::evaluate_rule` (currently just calls `match_clauses`), attempt the planner first:

```cpp
void Evaluator::evaluate_rule(const Rule& rule, FactDB& db) {
    auto plan = plan_rule(rule, db_, derived_facts_, pool_, symbol_formids_);
    if (plan) {
        if (plan->effect_op)  plan->effect_op->run(db);
        if (plan->derived_op) plan->derived_op->run(derived_facts_);
        return;
    }
    // Fallback: tuple-based
    Bindings bindings;
    auto order = compute_clause_order(rule);
    match_clauses(rule, order, 0, bindings, db);
}
```

Add `#include "mora/eval/rule_planner.h"` at the top of evaluator.cpp.

- [ ] **Step 5: Build**

```
xmake build 2>&1 | tail -10
```

Expected: succeeds. Everything still compiles.

### Task 1.5: Tests + verification

**Files:**
- Create: `tests/eval/test_binding_chunk.cpp`
- Create: `tests/eval/test_op_scan.cpp`
- Create: `tests/eval/test_rule_planner_single.cpp`

- [ ] **Step 1: `test_binding_chunk.cpp`**

Tests: construction, `index_of` hits + misses, append_row routes values to typed columns correctly, cell() round-trips, row_count reflects appends.

4–5 test cases, each ~15 lines. Cover String/Int/Keyword binding columns.

- [ ] **Step 2: `test_op_scan.cpp`**

Tests: Scan pulls all matching rows; constant-arg filter drops non-matching rows; symbol-resolved const works; duplicate-var equality filter works (`rel(?x, ?x)`); unresolved symbol → empty stream; chunking works (ingest > kChunkSize rows, expect multiple chunks).

5–6 test cases. Each sets up a small `ColumnarRelation`, builds a `FactPattern` via simple AST construction (use existing helpers; otherwise inline `FactPattern{...}` literals).

- [ ] **Step 3: `test_rule_planner_single.cpp`**

Tests:
- A simple effect rule (`set_gold_value(?npc, 100) :- gold_target(?npc)`): planner returns Some; after running, `skyrim/set` has one row per `gold_target` fact.
- A simple derived rule (`derived(?x) :- src(?x)`): planner returns Some; after running, `derived_facts_` has the expected rows. (Can only test this via a public hook on Evaluator, or by testing the planner's DerivedAppendOp directly.)
- A rule with a guard (`src(?x) :- other(?x), ?x > 10`): planner returns None (guards force fallback).
- A rule with a negated pattern: planner returns None.
- A rule with a BinaryExpr in effect args: planner returns None.

Use the real Evaluator in a one-rule module to exercise the integration:

```cpp
// Tiny .mora-like setup:
mora::StringPool pool;
mora::DiagBag diags;
mora::FactDB db(pool);
// seed input relation
auto src = pool.intern("gold_target");
db.configure_relation(src, /*arity*/ 1, /*indexed*/ {0});
db.add_fact(src, {mora::Value::make_formid(0xABC)});

// Parse and name-resolve the rule:
//   gold_target(?npc) :- => set gold_value(?npc, 100).
// (Use the same parse/resolve idiom as tests/eval/test_evaluator_effect_facts.cpp.)

mora::Evaluator eval(pool, diags, db);
eval.evaluate_module(mod, db);

auto rel_set = pool.intern("skyrim/set");
auto const* out = db.get_relation_columnar(rel_set);
ASSERT_NE(out, nullptr);
EXPECT_EQ(out->row_count(), 1u);
// assert tuple shape, keyword "GoldValue", value 100
```

- [ ] **Step 4: Build and run**

```
xmake build 2>&1 | tail -5
xmake run test_binding_chunk 2>&1 | tail -10
xmake run test_op_scan 2>&1 | tail -10
xmake run test_rule_planner_single 2>&1 | tail -10
xmake test 2>&1 | tail -5
```

Expected: all new tests pass + full suite at 79/79 (76 from Plan 12 + 3 new binaries).

- [ ] **Step 5: CLI smoke**

```
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p13-m1 --sink parquet.snapshot=/tmp/mora-p13-m1/out 2>&1 | tail -20
echo "exit: $?"
ls /tmp/mora-p13-m1/
```

Compare parquet file counts and names to Plan 12. Byte-identical is the gate — if the vectorized path produces different output for a given rule, root-cause it (usually: rule was already supported by MVP and hit the new path, producing output that differs from the tuple path for subtle reasons).

If the exact byte comparison is hard, use `xmake run test_cli_parquet_sink` — those tests lock in expected parquet shape and they MUST still pass.

### Task 1.6: M1 commit

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: vectorized evaluator MVP — single-clause positive rules

Introduces the chunk-at-a-time operator framework. BindingChunk carries
typed-column rows keyed by variable name. ScanOp reads a ColumnarRelation,
filters constant pattern positions + duplicate-var equality positions,
and projects into a BindingChunk. EffectAppendOp and DerivedAppendOp
consume the chunk stream and write effect facts / derived facts back
into FactDB.

A RulePlanner attempts to vectorize each rule. The MVP supports rules
with:
  - exactly one positive FactPattern body clause
  - a single Set-verb effect with simple args (var/literal/symbol), OR
  - a derived head whose args are all VariableExprs

Rules with guards, negation, InClause, conditional effects, or multi-
clause joins fall back to the tuple-based match_clauses — that's Plan 14
(coverage) and beyond.

No behavior change. Parquet output byte-identical to Plan 12.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — Multi-clause joins

Add `JoinOp` (hash-join) and extend the planner to multi-body-clause positive-conjunction rules. Rules that pass M1's single-clause filter still go through the same path; M2 widens the gate.

### Task 2.1: `JoinOp` — hash join on shared vars

**Files:**
- Create: `include/mora/eval/op_join.h`
- Create: `src/eval/op_join.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include "mora/eval/binding_chunk.h"
#include "mora/eval/operator.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mora {

// Hash-join two operator streams on shared variable columns. Build side
// is the "left" operator — fully drained and indexed on construction /
// first next_chunk() call. Probe side is "right" — iterated chunk-at-
// a-time, emitting one joined chunk per probe chunk (or fewer if the
// probe chunk doesn't hit anything).
//
// Output arity = union of left + right variable names. Shared variables
// appear once in the output (deduplicated).
class JoinOp : public Operator {
public:
    JoinOp(std::unique_ptr<Operator> left,
           std::unique_ptr<Operator> right);

    std::optional<BindingChunk> next_chunk() override;

private:
    std::unique_ptr<Operator>   left_;
    std::unique_ptr<Operator>   right_;

    // After build phase: left chunks + a hash index keyed on the shared
    // vars' row-values (hashed via Value::hash composed).
    std::vector<BindingChunk>                           left_chunks_;
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, uint32_t>>>
                                                         index_;
    std::vector<StringId>                               shared_vars_;
    std::vector<StringId>                               left_only_vars_;
    std::vector<StringId>                               out_var_names_;
    std::vector<const Type*>                            out_col_types_;
    bool built_ = false;

    void build_left();
    uint64_t hash_shared(const BindingChunk& chunk, size_t row) const;
    // For each probe row, emit matching join rows into out.
    void probe_row(const BindingChunk& probe, size_t probe_row,
                   BindingChunk& out);
};

} // namespace mora
```

- [ ] **Step 2: Write the implementation**

Implementation responsibilities:
- On first `next_chunk()`, call `build_left()`. That drains `left_` to completion, accumulating chunks, then builds a hash map keyed by the hashed concatenation of shared-var cell values. Map value = `vector<(chunk_idx, row_idx)>`.
- Shared vars = intersection of left's `names()` and right's `names()`. Determine this from peeking at the first chunk of each — or pass the shared-var list as a constructor argument. The plan lets you pick; the header uses peek-then-intersect; if that's awkward, pass them explicitly.

    **Recommended**: explicit. Change the constructor to take `std::vector<StringId> shared_vars`. Compute the intersection in the planner and pass it in.
- After build, on each `next_chunk()` pull from `right_`. For each row of the probe chunk, hash the shared-var cells, look up matches in the index, and emit one output row per match (concat: right row's cells + left chunk's left-only cells).
- Return `std::nullopt` when `right_` is exhausted.

This is the trickiest operator. Budget time for it and test thoroughly.

- [ ] **Step 3: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

### Task 2.2: Extend the planner to multi-clause

**File:** `src/eval/rule_planner.cpp`

- [ ] **Step 1: Relax the single-clause gate**

```cpp
// Old M1:
if (rule.body.size() != 1) return std::nullopt;

// M2:
if (rule.body.empty()) return std::nullopt;
```

- [ ] **Step 2: Build scan operators for every FactPattern**

Loop over `rule.body`, build a `ScanOp` for each. Track each scan's output var names.

Pick an order — for MVP, simplest:
1. Order by most-constants first (matches the current `compute_clause_order` selectivity heuristic).
2. Start with the first clause as the initial operator.
3. For each subsequent clause, compute shared vars with the cumulative-left's output names; wrap the cumulative-left with `JoinOp(left, right, shared)`.

Result: a left-deep operator tree.

- [ ] **Step 3: Feed the final operator tree into EffectAppendOp / DerivedAppendOp**

Unchanged from M1 — just hands the final operator pointer to the sink op.

- [ ] **Step 4: Build and run M1 tests**

```
xmake build 2>&1 | tail -5
xmake run test_rule_planner_single 2>&1 | tail -10
```

Expected: all M1 tests still pass — multi-clause planner is a superset.

### Task 2.3: Tests for multi-clause

**File:** `tests/eval/test_rule_planner_multi.cpp`

Cases:
- Two-clause positive rule with one shared var (canonical join test): e.g. `form/npc(?npc), gold_target(?npc, ?gold) => set gold_value(?npc, ?gold)`. Seed both relations; assert `skyrim/set` has the expected joined rows.
- Three-clause rule: chain of joins.
- A rule where no shared var exists between two clauses (Cartesian — planner should decline or emit Cartesian; pick one and document).
- A rule where the second clause has no matching rows in FactDB — result should be empty, not crash.
- Duplicate var across two clauses: `r1(?x), r2(?x, ?x)` — shared var `?x` + duplicate within `r2` handled by ScanOp's eq-filter.

4–6 test cases. Each seeds small relations and asserts the output `skyrim/set` shape.

### Task 2.4: CLI smoke + byte-identical parquet

- [ ] **Step 1: Run**

```
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p13-m2 --sink parquet.snapshot=/tmp/mora-p13-m2/out 2>&1 | tail -20
echo "exit: $?"
```

- [ ] **Step 2: Verify parquet suite still green**

```
xmake run test_cli_parquet_sink 2>&1 | tail -10
```

Expected: all 4 test_cli_parquet_sink cases pass. If a real rule in the test corpus got promoted from fallback to vectorized by M2 (likely, since most rules are multi-clause positive conjunctions) and output differs — investigate. The output should be identical; a diff indicates a join bug.

### Task 2.5: M2 commit

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: vectorized JoinOp + multi-clause positive-conjunction planner

JoinOp hash-joins two operator streams on shared variable names: left
is fully drained and indexed by build_left(); right is pulled chunk-
at-a-time and emits joined output rows per probe hit. Output arity is
the union of left and right binding-var names (deduplicated).

RulePlanner now accepts any number of positive FactPattern body clauses.
Clauses are ordered by selectivity (most constants first), then chained
left-deep via JoinOp on shared variable names. Effect + derived append
paths unchanged.

The MVP still rejects rules with guards, InClause, negation, or conditional
effects — those keep flowing through match_clauses. Plan 14 expands
coverage.

Parquet output byte-identical to Plan 12.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 13)

1. `xmake test` passes. Full suite count ~80+ (Plan 12's 76 + new operator tests).
2. CLI smoke on `test_data/example.mora` exits 0; parquet output byte-identical to Plan 12.
3. `test_cli_parquet_sink` (4 cases) passes — the strongest byte-level gate.
4. For rules the planner accepts, the evaluator runs the vectorized path; for others it falls back. Verify by adding a temporary log line in `Evaluator::evaluate_rule` during development, then removing.

## Forward-looking for Plan 14

- **Guards** (`GuardClause`): need a `FilterOp` that evaluates an `Expr` per-row over a `BindingChunk`. Per-row evaluation via a shim on the existing `resolve_expr` is fine for MVP; true vectorized expression eval (operate on whole columns at a time) is a later optimization.
- **Negated patterns**: need `AntiJoinOp` — like JoinOp but emits the left row IFF the probe side has NO match. Mechanically similar; same index.
- **InClause**: iterate list values to bind a variable (generator), or membership check against a bound variable. Modeled as a special scan op whose input is a list value from the current binding.
- **Conditional effects**: filter the binding chunk by the guard before the EffectAppendOp. Equivalent to `ConditionalEffect = Guard + Effect`.
- **Non-Set verbs** in effects: Add/Sub/Remove/Mul — just more output relation names. Extend the planner's switch.
- **LeveledEntries packing**: the current `apply_effects` special-case for 4-arg add with FormID value. Model as a specialized expression op or a one-off transform.

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/eval/binding_chunk.h` — **new**, the pipeline payload
- `/home/tbaldrid/oss/mora/include/mora/eval/operator.h` — **new**, pull-model base
- `/home/tbaldrid/oss/mora/include/mora/eval/op_scan.h` + `src/eval/op_scan.cpp` — **new**
- `/home/tbaldrid/oss/mora/include/mora/eval/op_append.h` + `src/eval/op_append.cpp` — **new**
- `/home/tbaldrid/oss/mora/include/mora/eval/op_join.h` + `src/eval/op_join.cpp` — **new** (M2)
- `/home/tbaldrid/oss/mora/include/mora/eval/rule_planner.h` + `src/eval/rule_planner.cpp` — **new**
- `/home/tbaldrid/oss/mora/src/eval/evaluator.cpp` — **modified**, `evaluate_rule` tries the planner first
- `/home/tbaldrid/oss/mora/include/mora/data/action_names.h` + `src/data/action_names.cpp` — **modified/new**, `action_to_field` moved to a free function the planner can reuse
