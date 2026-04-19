# Plan 14 — Vectorized evaluator coverage expansion

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Widen the rule shapes the vectorized planner accepts so more real rules route through operators instead of the tuple fallback. After Plan 14 the vectorized path handles: (a) non-Set verbs (Add/Sub/Remove/Multiply), (b) rules with multiple effects, (c) guards + conditional effects via a `FilterOp`, (d) negated fact patterns via `AntiJoinOp`, and (e) `InClause` via `InClauseOp` (both bound-var membership and unbound-var generator). Also: fix two latent correctness gaps flagged by the Plan 13 review before they bite.

**Architecture:** Same pull-model operators. New ops: `FilterOp` (per-row expression evaluation via a reusable free-function `resolve_expr`), `AntiJoinOp` (like `JoinOp` but emits left rows with NO match on the right), `InClauseOp` (two flavors: membership filter for bound-var, generator for unbound-var + list-typed RHS). `RulePlan` grows to hold `vector<EffectAppendOp>` — each effect gets its own fresh operator tree (re-scan per effect); Plan 15 can optimize via materialization if profiling demands it.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `678c0a9` (HEAD after Plan 13 M2, 30 commits above master)

**Scope note.** Plan 14 keeps the tuple fallback for everything it can't handle. The goal is to shrink the fallback's share, not eliminate it — Plan 15 does the final deletion.

**Non-negotiable invariant, same as Plans 11–13:** byte-identical parquet output. `test_cli_parquet_sink` 4/4 must stay green. If any rule gets promoted from fallback to vectorized and produces different output, it's a bug, not an acceptable divergence.

---

## Milestone 1 — Latent fixes + non-Set verbs + multiple effects per rule

Three small-to-medium changes in one commit. All three broaden coverage without introducing new operator types.

### Task 1.1: Fix `rule_planner.cpp`'s relation lookup to union `input_db` + `derived_facts`

**Files:**
- Modify: `src/eval/rule_planner.cpp`

Today the planner at ~line 158 picks ONE source (input first, derived as fallback). `Evaluator::merged_query` at `src/eval/evaluator.cpp:263-277` unions both. The vectorized path silently misses derived rows when a relation name exists in both DBs.

- [ ] **Step 1: Introduce a `MergedRelation` helper**

Two paths:

(a) **Preferred — implement `MergedRelation` as a thin operator wrapper.** Walk the cleaner path: build two `ScanOp`s (one per source) and wrap them with a `UnionOp` that concatenates their chunk streams. Add `include/mora/eval/op_union.h` + `src/eval/op_union.cpp`:

```cpp
// Emit all chunks from left, then all chunks from right.
class UnionOp : public Operator {
public:
    UnionOp(std::unique_ptr<Operator> left,
            std::unique_ptr<Operator> right);
    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override;
private:
    std::unique_ptr<Operator> left_;
    std::unique_ptr<Operator> right_;
    bool left_exhausted_ = false;
};
```

`output_var_names()` returns `left_->output_var_names()` — both sources have the same pattern, same var names. Assertion in the ctor: right's names must match left's.

Implementation is trivial: pull from left until exhaustion, then switch to right.

(b) Alternative: have the planner emit two parallel operator trees and run both sequentially. Uglier — prefer (a).

- [ ] **Step 2: Planner uses `UnionOp` when both DBs have the relation**

In `plan_rule`, replace the current `get_relation_columnar` logic:

```cpp
// Was:
const ColumnarRelation* rel = input_db.get_relation_columnar(rel_name);
if (rel == nullptr) rel = derived_facts.get_relation_columnar(rel_name);
auto scan = ScanOp::build(rel, fp, pool, symbol_formids);
```

With:

```cpp
const ColumnarRelation* rel_input   = input_db.get_relation_columnar(rel_name);
const ColumnarRelation* rel_derived = derived_facts.get_relation_columnar(rel_name);

std::unique_ptr<Operator> source;
if (rel_input && rel_derived) {
    // Both exist — scan each and union.
    auto scan_in  = ScanOp::build(rel_input,   fp, pool, symbol_formids);
    auto scan_de  = ScanOp::build(rel_derived, fp, pool, symbol_formids);
    source = std::make_unique<UnionOp>(std::move(scan_in), std::move(scan_de));
} else {
    source = ScanOp::build(rel_input ? rel_input : rel_derived,
                            fp, pool, symbol_formids);
}
```

`UnionOp`'s ctor asserts both sides have compatible output names. The two ScanOps produce identical names by construction (same `FactPattern`), so this is safe.

- [ ] **Step 3: Test for the fix**

In `tests/eval/test_rule_planner_single.cpp` (or a new file), add a test: seed `input_db` with 1 row for relation `X`, seed `derived_facts` with 1 different row for relation `X`, run a rule that scans `X`, assert the output has contributions from both.

Simplest way: fake the derived-facts state by evaluating a simple derived rule first, then a rule that reads the same relation. Or expose a test-only helper on `Evaluator` to inject derived facts. Use whichever matches the existing test idiom.

### Task 1.2: Fix `spec_from_expr` to resolve `KeywordLiteral` through `symbol_formids`

**Files:**
- Modify: `src/eval/rule_planner.cpp`

The tuple-path `resolve_expr` at `src/eval/evaluator.cpp:542-549` treats `:Alias` keywords as a FormID-lookup path (legacy `:Symbol → FormID`). The planner's `spec_from_expr` doesn't.

- [ ] **Step 1: Update `spec_from_expr`**

```cpp
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
    else if (auto const* sl = std::get_if<StringLiteral>(&e.data)) s.constant = Value::make_string(sl->value);
    else if (auto const* kl = std::get_if<KeywordLiteral>(&e.data)) {
        // Match the tuple-path's resolve_expr: if this keyword has an
        // EditorID-to-FormID mapping, prefer the FormID. Otherwise it
        // stays a keyword.
        auto it = symbols.find(kl->value.index);
        s.constant = (it != symbols.end())
            ? Value::make_formid(it->second)
            : Value::make_keyword(kl->value);
    } else if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        auto it = symbols.find(se->name.index);
        s.constant = it == symbols.end()
            ? Value::make_var()
            : Value::make_formid(it->second);
    }
    return s;
}
```

- [ ] **Step 2: Test**

Add a test where a rule uses `:Alias` as an effect arg, the symbol map has the alias pre-loaded, and the vectorized output matches the tuple output. Use the same side-channel counter idiom to assert which path fires.

### Task 1.3: Extend verb switch — Add / Sub / Remove / Multiply

**Files:**
- Modify: `src/eval/rule_planner.cpp`

The M1 planner only accepts `VerbKind::Set`. Expand:

- [ ] **Step 1: Update the verb-to-relation switch**

```cpp
StringId out_rel;
switch (eff.verb) {
    case VerbKind::Set:      out_rel = pool.intern("skyrim/set");      break;
    case VerbKind::Add:      out_rel = pool.intern("skyrim/add");      break;
    case VerbKind::Sub:      out_rel = pool.intern("skyrim/sub");      break;
    case VerbKind::Remove:   out_rel = pool.intern("skyrim/remove");   break;
    default:                 return std::nullopt;  // unknown verb → fallback
}
```

Note `VerbKind::Mul` — check the enum values in `include/mora/ast/ast.h`. If present, map to `skyrim/multiply`. If not, omit.

- [ ] **Step 2: `action_to_field` must accept the non-Set actions**

The free function at `src/data/action_names.cpp` already handles the full verb set (walks `kFields`, `kFormArrays`, `kFlags`, multiply ops). Verify by reading. If any non-Set action maps to `FieldOp::Invalid`, add cases.

Note: `VerbKind::Remove` for array-valued fields maps to `FieldOp::Remove`. The action_to_field function should already handle this via `kFormArrays[i].remove_action`.

- [ ] **Step 3: Check effect relations are configured with non-Set names**

In `Evaluator::ensure_effect_relations_configured` at `src/eval/evaluator.cpp:25-49`, the `skyrim/sub` relation may not be configured if that verb wasn't used before. Verify:

```
grep -n 'skyrim/sub\|skyrim/multiply' src extensions
```

If `skyrim/sub` isn't configured, add it. The current set is `skyrim/{set,add,remove,multiply}`. If Sub gets folded into Set (check — it might be a no-op verb in Mora's grammar), skip. Otherwise add `effect_rel_sub_` alongside.

Check the parser (`src/parser/parser.cpp`) or grammar docs to confirm `VerbKind::Sub` is a real parse target. If it's not, omit from the switch. Look for the `sub_*` prefix in `src/data/action_names.h` — if missing, skip this whole subverb.

### Task 1.4: Multiple effects per rule — re-scan strategy

**Files:**
- Modify: `include/mora/eval/rule_planner.h`
- Modify: `src/eval/rule_planner.cpp`
- Modify: `src/eval/evaluator.cpp`

Today the planner rejects rules with > 1 effect. Support them by building a fresh operator tree per effect.

- [ ] **Step 1: Update `RulePlan`**

```cpp
struct RulePlan {
    std::vector<std::unique_ptr<EffectAppendOp>>  effect_ops;
    std::unique_ptr<DerivedAppendOp>              derived_op;
};
```

- [ ] **Step 2: Refactor planner body-tree construction into a helper**

Extract the body-planning code (the piece that produces the `unique_ptr<Operator>` representing the joined body) into a helper:

```cpp
static std::optional<std::unique_ptr<Operator>> plan_body(
    const Rule& rule,
    const FactDB& input_db,
    const FactDB& derived_facts,
    StringPool& pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids);
```

The helper returns nullopt for unsupported body shapes. The main `plan_rule` calls it once per effect (re-scan) and once for the derived-head path.

- [ ] **Step 3: Build N EffectAppendOps**

In `plan_rule`, replace the single-effect branch with:

```cpp
if (!rule.effects.empty()) {
    std::vector<std::unique_ptr<EffectAppendOp>> effect_ops;
    effect_ops.reserve(rule.effects.size());

    for (const Effect& eff : rule.effects) {
        if (eff.args.size() != 2) return std::nullopt;
        if (!is_simple_arg_expr(eff.args[0]) || !is_simple_arg_expr(eff.args[1]))
            return std::nullopt;

        // Verb → relation mapping (Task 1.3).
        StringId out_rel;
        switch (eff.verb) { /* ... */ }

        // Legacy action name + action_to_field (unchanged).
        std::string legacy = std::string(verb_prefix(eff.verb)) +
                             std::string(pool.get(eff.name));
        StringId const action_id = pool.intern(legacy);
        auto [field, op] = action_to_field(action_id, pool);
        if (field == FieldId::Invalid) return std::nullopt;

        // Re-plan the body for this effect.
        auto body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
        if (!body) return std::nullopt;

        StringId const field_kw_id = pool.intern(field_id_name(field));
        effect_ops.push_back(std::make_unique<EffectAppendOp>(
            std::move(*body), out_rel, field_kw_id,
            spec_from_expr(eff.args[0], pool, symbol_formids),
            spec_from_expr(eff.args[1], pool, symbol_formids)));
    }

    RulePlan plan;
    plan.effect_ops = std::move(effect_ops);
    return plan;
}
```

Note: `verb_prefix` is already defined inside `src/eval/evaluator.cpp`. Promote it to the shared `include/mora/data/action_names.h` header as a free function so the planner can reuse it.

- [ ] **Step 4: Evaluator runs all effect_ops**

In `Evaluator::evaluate_rule`:

```cpp
if (plan) {
    for (auto& op : plan->effect_ops) op->run(db);
    if (plan->derived_op) plan->derived_op->run(derived_facts_);
    ++vectorized_rules_count_;
    return;
}
```

### Task 1.5: Tests + verification

**Files:**
- Create: `tests/eval/test_planner_merged_lookup.cpp` (or extend an existing test file)
- Modify: `tests/eval/test_rule_planner_single.cpp` — add keyword-alias effect arg test + non-Set verb test + multi-effect test

- [ ] **Step 1: Cover each fix**

Minimum new test cases (4–6 total):
1. A rule scanning a relation that exists only in `derived_facts` — still works vectorized.
2. A rule scanning a relation that exists in both — output is the union.
3. A rule with a `:Alias` effect arg that maps to a FormID — vectorized output matches tuple.
4. A rule with an `add` verb — routes to `skyrim/add`.
5. A rule with two effects — both appear in the right relations.

Drop cases whose subject is already covered by existing tests.

- [ ] **Step 2: Byte-identical parquet gate**

```
xmake run test_cli_parquet_sink 2>&1 | tail -10
```

All 4 must pass. Then CLI smoke:

```
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p14-m1 --sink parquet.snapshot=/tmp/mora-p14-m1/out
echo "exit: $?"
```

Diff against Plan 13's output:

```
# On HEAD 678c0a9, before your M1 changes
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p13 --sink parquet.snapshot=/tmp/mora-p13/out

# On your M1
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p14-m1 --sink parquet.snapshot=/tmp/mora-p14-m1/out

diff -r /tmp/mora-p13/out /tmp/mora-p14-m1/out
```

Expected: no diffs. If a rule gets promoted to vectorized and produces different bytes, it's a bug — root-cause, fix, re-test.

- [ ] **Step 3: Vectorized-count sanity**

`Evaluator::vectorized_rules_count()` should rise for `test_data/example.mora`:
- After Plan 13 M2: 1/5 rules vectorized (just `bandit`).
- After Plan 14 M1: 3/5 vectorized (`bandit`, `tag_bandits`, `iron_weapons` — all their body shapes are positive conjunctions; `tag_bandits` uses `add` which M1 now supports).

The other 2 (`silver_weapons`, `elite_bandits`) still fall back — they need M2/M3.

If the math doesn't match, investigate — maybe a rule I've miscounted has an unsupported detail.

### Task 1.6: M1 commit

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

Expected: all tests pass. Total test count = Plan 13's 80 + ~5 new cases (may or may not add new binaries; probably extends existing).

- [ ] **Step 2: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: vectorized planner — merged lookup, non-Set verbs, multiple effects

Three changes widen the vectorized path's coverage:

- rule_planner's relation lookup now unions input_db and derived_facts
  via a new UnionOp, matching Evaluator::merged_query semantics. Fixes a
  latent correctness gap flagged in Plan 13 review.
- spec_from_expr for KeywordLiteral effect args now resolves through
  symbol_formids (legacy :Alias → FormID). Matches Evaluator::resolve_expr.
- Verb switch extended from Set-only to Set/Add/Sub/Remove/Multiply;
  each routes to the matching skyrim/{verb} relation.
- RulePlan grows from a single EffectAppendOp to vector<EffectAppendOp>;
  rules with N effects build N operator trees (re-scan strategy).

Vectorized rule count on test_data/example.mora: 1/5 → 3/5. Parquet
output byte-identical. Guards, negation, InClause remain tuple-path
fallback — Plan 14 M2/M3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — FilterOp (guards + conditional effects)

Add per-row expression evaluation as a new operator. Use it for `GuardClause` in rule bodies and `ConditionalEffect` at effect emission time.

### Task 2.1: Extract `resolve_expr` + `evaluate_guard` as free functions

**Files:**
- Modify: `include/mora/eval/evaluator.h` (reduce `resolve_expr` to a thin wrapper)
- Create: `include/mora/eval/expr_eval.h`
- Create: `src/eval/expr_eval.cpp`

Follow the `action_to_field` pattern from Plan 13 M1.

- [ ] **Step 1: Write `expr_eval.h`**

```cpp
#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/data/value.h"

#include <unordered_map>

namespace mora {

using Bindings = std::unordered_map<uint32_t, Value>;

// Pure-functional expression evaluation. Takes all state it needs
// explicitly — matches the tuple-path `Evaluator::resolve_expr` behavior
// for all expression kinds (VariableExpr, SymbolExpr, literals, BinaryExpr,
// CallExpr, FieldAccessExpr, EditorIdExpr).
//
// If `bindings` doesn't have a variable or `symbols` doesn't have a symbol,
// returns Value::make_var() — same behavior as the tuple path.
Value resolve_expr(const Expr& e,
                    const Bindings& bindings,
                    StringPool& pool,
                    const std::unordered_map<uint32_t, uint32_t>& symbols);

// Boolean coercion of resolve_expr for guards.
bool evaluate_guard(const Expr& e,
                     const Bindings& bindings,
                     StringPool& pool,
                     const std::unordered_map<uint32_t, uint32_t>& symbols);

} // namespace mora
```

- [ ] **Step 2: Move the implementation**

Port the bodies of `Evaluator::resolve_expr` (lines 499-625 of `src/eval/evaluator.cpp`) and `Evaluator::evaluate_guard` (probably at lines 380-400) into the free functions, adapting for the explicit `pool` + `symbols` arguments instead of member access.

Keep member thin wrappers that delegate:

```cpp
// In evaluator.cpp
Value Evaluator::resolve_expr(const Expr& expr, const Bindings& bindings) {
    return mora::resolve_expr(expr, bindings, pool_, symbol_formids_);
}

bool Evaluator::evaluate_guard(const Expr& expr, const Bindings& bindings) {
    return mora::evaluate_guard(expr, bindings, pool_, symbol_formids_);
}
```

Add `#include "mora/eval/expr_eval.h"` in evaluator.cpp.

- [ ] **Step 3: Build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -10
```

Expected: all tests still pass. Behavior unchanged — just extracted.

### Task 2.2: Create `FilterOp`

**Files:**
- Create: `include/mora/eval/op_filter.h`
- Create: `src/eval/op_filter.cpp`

- [ ] **Step 1: Write the header**

```cpp
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
             const Expr*                predicate,
             StringPool&                pool,
             const std::unordered_map<uint32_t, uint32_t>& symbols);

    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override {
        return input_->output_var_names();
    }

private:
    std::unique_ptr<Operator>                            input_;
    const Expr*                                           predicate_;
    StringPool&                                           pool_;
    const std::unordered_map<uint32_t, uint32_t>&         symbols_;
};

} // namespace mora
```

- [ ] **Step 2: Write the implementation**

```cpp
#include "mora/eval/op_filter.h"

#include "mora/eval/expr_eval.h"

#include <stdexcept>

namespace mora {

FilterOp::FilterOp(std::unique_ptr<Operator> input,
                    const Expr*                predicate,
                    StringPool&                pool,
                    const std::unordered_map<uint32_t, uint32_t>& symbols)
    : input_(std::move(input))
    , predicate_(predicate)
    , pool_(pool)
    , symbols_(symbols)
{}

std::optional<BindingChunk> FilterOp::next_chunk() {
    while (auto in_chunk = input_->next_chunk()) {
        BindingChunk& chunk = *in_chunk;

        // Build a new chunk with the same shape, copy rows that pass.
        std::vector<const Type*> out_types;
        out_types.reserve(chunk.arity());
        for (size_t i = 0; i < chunk.arity(); ++i) {
            out_types.push_back(chunk.column(i).type());
        }
        BindingChunk out(chunk.names(), out_types);

        for (size_t row = 0; row < chunk.row_count(); ++row) {
            // Build per-row Bindings.
            Bindings b;
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
        // Else: all rows filtered out — pull next chunk.
    }
    return std::nullopt;
}

} // namespace mora
```

- [ ] **Step 3: Build**

```
xmake build mora_lib 2>&1 | tail -5
```

### Task 2.3: Planner — route `GuardClause` + `ConditionalEffect` through `FilterOp`

**Files:**
- Modify: `src/eval/rule_planner.cpp`

- [ ] **Step 1: Relax `body_is_positive_conjunction` to accept `GuardClause`**

```cpp
bool body_is_supported_for_vectorized(const Rule& rule) {
    for (auto const& clause : rule.body) {
        bool ok = std::visit([](auto const& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) return !c.negated;
            if constexpr (std::is_same_v<T, GuardClause>) return true;
            return false;  // InClause/Effect/ConditionalEffect → still no in M2
        }, clause.data);
        if (!ok) return false;
    }
    return true;
}
```

- [ ] **Step 2: Update `plan_body` to splice `FilterOp`s**

After the joins are built, iterate over `rule.body` in its original order and wrap the cumulative operator in a `FilterOp` whenever a `GuardClause` is encountered:

```cpp
// After constructing the join tree from FactPatterns, re-walk the body
// to splice in guards.
for (auto const& clause : rule.body) {
    if (auto const* g = std::get_if<GuardClause>(&clause.data)) {
        cumulative = std::make_unique<FilterOp>(
            std::move(cumulative), g->expr.get(), pool, symbol_formids);
    }
}
```

Ordering note: guards can reference variables only after they're bound. The current tuple path's `compute_clause_order` reorders to ensure this. For the vectorized path, the simplest policy is: **apply all guards AFTER all scans/joins**. That means every bound variable is available. Less efficient (doesn't push guard predicates down into selective scans) but correct. Optimization is Plan 15+.

- [ ] **Step 3: Conditional effects — treat as `FilterOp` + `EffectAppendOp`**

In the effect-planning loop (Task 1.4), also iterate `rule.conditional_effects`:

```cpp
for (const ConditionalEffect& ce : rule.conditional_effects) {
    if (ce.effect.args.size() != 2) return std::nullopt;
    if (!is_simple_arg_expr(ce.effect.args[0]) || !is_simple_arg_expr(ce.effect.args[1]))
        return std::nullopt;

    // Verb + action_to_field (same as unconditional effect).
    /* ... */

    auto body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
    if (!body) return std::nullopt;

    // Wrap body in the conditional's guard.
    auto filtered = std::make_unique<FilterOp>(
        std::move(*body), ce.guard.get(), pool, symbol_formids);

    StringId field_kw_id = pool.intern(field_id_name(field));
    effect_ops.push_back(std::make_unique<EffectAppendOp>(
        std::move(filtered), out_rel, field_kw_id,
        spec_from_expr(ce.effect.args[0], pool, symbol_formids),
        spec_from_expr(ce.effect.args[1], pool, symbol_formids)));
}
```

### Task 2.4: Tests

**Files:**
- Create: `tests/eval/test_op_filter.cpp`
- Create: `tests/eval/test_planner_guards.cpp`

Minimum cases:
- **Filter alone**: seed a chunk of 10 rows, filter by `x > 5`, expect 5 rows.
- **Guard in a rule**: `e(?x) :- s(?x), ?x > 10 => set gold_value(?x, 100)` — vectorized path produces the correct `skyrim/set` rows.
- **Multiple guards**: two guards in the same rule; both apply.
- **Conditional effect**: `e(?x) :- s(?x), ?x > 10 => set gold_value(?x, 100)` (as ConditionalEffect) — vectorized path produces the same output.

3–5 cases total. Use the parse+resolve idiom for end-to-end integration.

### Task 2.5: M2 commit

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

- [ ] **Step 2: Byte-identical parquet**

Same drill as M1 Task 1.5 Step 2. `test_cli_parquet_sink` 4/4.

- [ ] **Step 3: Vectorized count**

After M2, the `elite_bandits(NPC)` rule (with `Level >= 20` guard) should now vectorize. Vectorized count: 4/5 on `test_data/example.mora`.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: vectorized FilterOp — guards + conditional effects

Introduces FilterOp (per-row Expr evaluation via a new free-function
expr_eval::resolve_expr + evaluate_guard extracted from Evaluator).
Planner now accepts GuardClauses in rule bodies and routes each
ConditionalEffect as FilterOp(guard) → EffectAppendOp(effect).

Per-row Bindings construction is the slow path in the MVP; a vectorized
expression evaluator is a Plan 15 optimization if profiling demands it.

Guards are applied AFTER all scans/joins in the operator tree — correct
but doesn't push predicates down to scan-time filters. Optimization
deferred.

Vectorized rule count on test_data/example.mora: 3/5 → 4/5. Byte-identical
parquet output preserved.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 3 — Negation (`AntiJoinOp`) + `InClause`

Add the last two operators. After M3, the vectorized path covers every rule shape in `test_data/example.mora` and the large majority of real rules.

### Task 3.1: `AntiJoinOp`

**Files:**
- Create: `include/mora/eval/op_antijoin.h`
- Create: `src/eval/op_antijoin.cpp`

Mirror `JoinOp` but emit left rows IFF the right side has NO match on the shared vars.

- [ ] **Step 1: Write the header + impl**

Same shape as `JoinOp`: two-operator constructor, drain right side at first `next_chunk`, build hash index on shared-var cells. For each left row, lookup; if not found → emit.

`output_var_names()` returns `left_->output_var_names()` (right side contributes nothing to the output).

Build phase indexes the RIGHT (the existence-probe side). Probe phase reads left chunk-at-a-time.

### Task 3.2: Planner — negated patterns → `AntiJoinOp`

**Files:**
- Modify: `src/eval/rule_planner.cpp`

- [ ] **Step 1: Relax `body_is_supported_for_vectorized`**

Allow negated `FactPattern`s too.

- [ ] **Step 2: Splice `AntiJoinOp`s**

After the positive-join tree is built, iterate negated FactPatterns in body order:

```cpp
for (auto const& clause : rule.body) {
    if (auto const* fp = std::get_if<FactPattern>(&clause.data); fp && fp->negated) {
        // Build a scan for the negated relation + pattern (same merge
        // logic as positive — use UnionOp for input+derived).
        auto neg_source = build_source_for_fact_pattern(*fp, input_db,
            derived_facts, pool, symbol_formids);
        // Shared vars = intersection of current cumulative output with
        // neg source's pattern vars.
        auto shared = intersect_var_names(cumulative->output_var_names(),
                                           neg_source->output_var_names());
        if (shared.empty()) {
            // Anti-Cartesian — skip negated clause (rule vacuously false
            // if right has any rows; trivially true otherwise). For MVP,
            // decline to plan.
            return std::nullopt;
        }
        cumulative = std::make_unique<AntiJoinOp>(
            std::move(cumulative), std::move(neg_source), shared);
    }
}
```

Factor `build_source_for_fact_pattern` (the Scan + Union logic from Task 1.1 Step 2) into a reusable helper.

### Task 3.3: `InClauseOp` — two flavors

**Files:**
- Create: `include/mora/eval/op_in_clause.h`
- Create: `src/eval/op_in_clause.cpp`

Two modes:
- **Generator**: unbound var + list-typed RHS → emit one row per list element, binding the var to that element.
- **Membership filter**: bound var + list-typed or literal-set RHS → emit rows where var's value is in the list.

The tuple-path logic is at `src/eval/evaluator.cpp:199-229`. Port to vectorized.

- [ ] **Step 1: Write the op**

```cpp
// Generator form: for each input row, resolve the "values" expression
// to a List Value. For each element in that list, emit an output row
// = input row with an added column binding the generator var.
//
// Membership form: for each input row, resolve the values expression
// to a List Value. If the input row's generator-var cell is in the list,
// emit the row. Otherwise drop it.
class InClauseOp : public Operator {
public:
    // Generator form
    static std::unique_ptr<InClauseOp> build_generator(
        std::unique_ptr<Operator> input,
        StringId                   var_name,
        const Expr*                values_expr,
        StringPool&                pool,
        const std::unordered_map<uint32_t, uint32_t>& symbols);

    // Membership form
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
    // ...
};
```

Per-row behavior:
- Build a Bindings map from the current row.
- `resolve_expr(values_expr, bindings, pool, symbols)` → should be a List.
- If List's `kind()` isn't List, decline — skip row (matches tuple-path fallthrough).

### Task 3.4: Planner — InClause

**Files:**
- Modify: `src/eval/rule_planner.cpp`

- [ ] **Step 1: Relax `body_is_supported_for_vectorized`**

Allow `InClause`.

- [ ] **Step 2: Determine generator vs membership**

For each InClause in the body:
- If the clause variable is already bound by previous scans → membership.
- Otherwise → generator.

Track a "bound var set" as the planner walks the body. Extract from `cumulative->output_var_names()` if the cumulative operator exists; bootstrap with empty if InClause comes first.

### Task 3.5: Tests

**Files:**
- Create: `tests/eval/test_op_antijoin.cpp`
- Create: `tests/eval/test_op_in_clause.cpp`
- Extend: `tests/eval/test_rule_planner_multi.cpp` with negation + InClause cases.

Minimum cases (6–8 total):
- **AntiJoin alone**: left (2 rows), right (matches 1 of them) → output has 1 left row.
- **Negated rule**: `safe(?w) :- form/weapon(?w), not dangerous(?w)` → correct rows.
- **InClause generator**: `has_kw(?w, ?kw) :- form/weapon(?w), ?kw in keyword_list(?w)` — one row per keyword per weapon.
- **InClause membership**: `has_specific_kw(?w) :- form/weapon(?w), @Silver in keyword_list(?w)` — rows where the list contains the specific keyword.

### Task 3.6: M3 commit

- [ ] **Step 1: Full build + test + byte-identical parquet**

Same drill as M1/M2.

- [ ] **Step 2: Vectorized count**

After M3, **all 5 rules** in `test_data/example.mora` should vectorize. The `silver_weapons(Weapon)` rule (with negated pattern) now works.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: vectorized AntiJoinOp + InClauseOp — negation + list membership

AntiJoinOp mirrors JoinOp but emits left rows IFF the right side has
NO match on shared vars. Builds the hash index over the right (probe)
side; left flows chunk-at-a-time. Planner routes negated FactPatterns
through AntiJoin.

InClauseOp has two forms: generator (unbound var + list RHS → emit one
row per list element) and membership (bound var + list RHS → keep row
if var's value is in the list). Planner picks based on whether the var
is already bound by previous scans.

Anti-Cartesian (negated clause with no shared vars with prior bindings)
falls back to tuple. Same restriction as the positive Cartesian case.

Vectorized rule count on test_data/example.mora: 4/5 → 5/5. All corpus
rules now route through the vectorized path. Byte-identical parquet.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 14)

1. `xmake test` passes (count ~85+).
2. `test_cli_parquet_sink` 4/4 passes.
3. CLI smoke on `test_data/example.mora` byte-identical to Plan 12.
4. `Evaluator::vectorized_rules_count()` == total rule count for `test_data/example.mora` (5/5).
5. Branch is 33 commits above master (30 + 3 Plan 14 commits).

## Forward-looking for Plan 15

- **Delete the tuple fallback.** Verify via the counter: `vectorized_rules_count()` == `total_rules` across every test fixture and `test_data/` corpus. If anything still falls back, investigate and add vectorized support before deletion.
- **Seminaive fixpoint.** Replace the current "evaluate rules in parse order, rely on clause ordering" approach with proper delta-driven iteration. Matters for rules that transitively depend on derived facts.
- **Vectorized expression eval.** Per-row `Bindings` construction in `FilterOp` is the slow path. A column-at-a-time expression evaluator would be faster (operate on a chunk's columns instead of scalar Values).
- **Arrow zero-copy sink.** Plan 12 noted this; Plan 15 or later does it.
- **Cartesian joins** — currently fall back. Plan 15 could support or stay with fallback-then-delete.
- **Multi-effect rules without re-scan.** Materialize body results in a temp BindingChunk buffer; feed each effect from the buffer. Saves N-1 body re-evaluations.

## Critical files

- `/home/tbaldrid/oss/mora/src/eval/rule_planner.cpp` — **heavily modified** across all three milestones
- `/home/tbaldrid/oss/mora/include/mora/eval/rule_planner.h` — **modified** (RulePlan shape)
- `/home/tbaldrid/oss/mora/include/mora/eval/op_union.h` + `src/eval/op_union.cpp` — **new** (M1)
- `/home/tbaldrid/oss/mora/include/mora/eval/expr_eval.h` + `src/eval/expr_eval.cpp` — **new** (M2)
- `/home/tbaldrid/oss/mora/include/mora/eval/op_filter.h` + `src/eval/op_filter.cpp` — **new** (M2)
- `/home/tbaldrid/oss/mora/include/mora/eval/op_antijoin.h` + `src/eval/op_antijoin.cpp` — **new** (M3)
- `/home/tbaldrid/oss/mora/include/mora/eval/op_in_clause.h` + `src/eval/op_in_clause.cpp` — **new** (M3)
- `/home/tbaldrid/oss/mora/src/eval/evaluator.cpp` — **modified** (evaluate_rule runs N effect_ops; `resolve_expr`/`evaluate_guard` now delegate to free functions)
