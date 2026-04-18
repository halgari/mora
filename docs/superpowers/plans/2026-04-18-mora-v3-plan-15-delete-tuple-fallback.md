# Plan 15 — Close vectorized gaps + delete tuple fallback

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the vectorized evaluator the only evaluator. Close the remaining gaps Plan 14 deferred (`EditorIdExpr` in args, expression-valued effect args via `BinaryExpr`/`CallExpr`/`FieldAccessExpr`, multi-value `InClause` literal sets), add a naive fixpoint driver for recursive derived rules, then delete `match_clauses`, `match_fact_pattern`, `merged_query`, `compute_clause_order`, the fallback branch in `evaluate_rule`, the side-channel `vectorized_rules_count_` counter, and the member-function shims over `resolve_expr`/`evaluate_guard`.

**Architecture:** Two milestones. M1 closes gaps — once `vectorized_rules_count() == total_rules` across every test fixture, the fallback path is dead weight. M2 deletes it plus all its helpers, renames nothing, keeps the pure-vectorized shape. Seminaive fixpoint is NOT in scope — naive iteration (run all rules until no derived-facts growth) is enough for Mora's test corpus and the master spec's "seminaive" refinement can be a post-v3-rewrite polish if profiling demands it.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `7a2268e` (HEAD after Plan 14 M3, 33 commits above master)

**Non-negotiable invariants:**
1. **Byte-identical parquet output.** `test_cli_parquet_sink` 4/4 across both milestones. Plan 15 is a pure refactor — the last rule shapes to migrate will produce identical output to the tuple path they replace.
2. **All rules vectorize.** By end of M1, `Evaluator::vectorized_rules_count()` must equal the total rule count for every test fixture. M2 depends on this — if anything still falls back when M2 deletes the fallback, that rule will mis-evaluate.

---

## Milestone 1 — Close the remaining vectorized gaps + naive fixpoint

### Task 1.1: `EditorIdExpr` in pattern args + effect args

**Files:**
- Modify: `src/eval/op_scan.cpp`
- Modify: `src/eval/rule_planner.cpp`

`EditorIdExpr` carries a `StringId id` — an `@EditorID` reference like `@WeapMaterialSilver`. It resolves to a `FormID` via the `symbol_formids` map (same mechanism as `SymbolExpr` and `KeywordLiteral`).

- [ ] **Step 1: `ScanOp::build` recognizes `EditorIdExpr`**

In the pattern-args walk at `src/eval/op_scan.cpp:~45`, add a branch after `SymbolExpr`:

```cpp
} else if (auto const* eid = std::get_if<EditorIdExpr>(&arg.data)) {
    auto sit = symbol_formids.find(eid->id.index);
    if (sit == symbol_formids.end()) {
        op->no_match_ = true;
        return op;
    }
    op->const_pos_.push_back({Value::make_formid(sit->second), i});
}
```

Same semantics as `SymbolExpr`: unresolved → no-match; resolved → FormID constant filter.

- [ ] **Step 2: `spec_from_expr` recognizes `EditorIdExpr`**

In `src/eval/rule_planner.cpp`, after the `SymbolExpr` branch:

```cpp
} else if (auto const* eid = std::get_if<EditorIdExpr>(&e.data)) {
    auto it = symbols.find(eid->id.index);
    s.constant = (it != symbols.end())
        ? Value::make_formid(it->second)
        : Value::make_var();
}
```

- [ ] **Step 3: `is_simple_arg_expr` accepts `EditorIdExpr`**

Add `|| std::holds_alternative<EditorIdExpr>(e.data)` to the predicate at the top of `rule_planner.cpp`.

- [ ] **Step 4: Build + verify**

```
xmake build 2>&1 | tail -5
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p15-m1a --sink parquet.snapshot=/tmp/mora-p15-m1a/out 2>&1 | tail -10
```

After this step, `vectorized_rules_count()` for `test_data/example.mora` should jump from 0/5 to some non-zero number (likely most of the 5 rules — the ones whose remaining barriers were just `@EditorID` args). Check by adding a temporary log line or running `test_cli_parquet_sink`.

### Task 1.2: `BinaryExpr` / `CallExpr` / `FieldAccessExpr` in **effect args** via `EffectArgSpec::Kind::Expr`

**Files:**
- Modify: `include/mora/eval/op_append.h`
- Modify: `src/eval/op_append.cpp`
- Modify: `src/eval/rule_planner.cpp`

Effect args (target + value of a `Set`/`Add`/etc. effect) sometimes compute — e.g. `set gold_value(?npc, ?base * 2)`. The `EffectAppendOp::resolve_spec` currently handles only Var and Constant. Add an Expr path that evaluates per-row via the free-function `resolve_expr`.

- [ ] **Step 1: Extend `EffectArgSpec`**

```cpp
struct EffectArgSpec {
    enum class Kind { Var, Constant, Expr };
    Kind                         kind;
    StringId                     var_name;   // when kind == Var
    Value                        constant;   // when kind == Constant
    const mora::Expr*            expr;       // when kind == Expr
};
```

- [ ] **Step 2: Extend `EffectAppendOp::run` and `DerivedAppendOp::run`**

Both use a `resolve_spec(spec, chunk, row)` helper. Update to handle `Kind::Expr`:

```cpp
case EffectArgSpec::Kind::Expr: {
    // Build per-row Bindings from the chunk.
    Bindings b;
    for (size_t col = 0; col < chunk.arity(); ++col) {
        b[chunk.name_at(col).index] = chunk.cell(row, col);
    }
    return resolve_expr(*spec.expr, b, pool_, symbols_);
}
```

This requires `EffectAppendOp` / `DerivedAppendOp` to carry a `StringPool&` + `const unordered_map<uint32_t, uint32_t>&` for the free-function. Update their constructors.

- [ ] **Step 3: Pass `pool` + `symbol_formids` through from the planner**

The planner constructs `EffectAppendOp` and `DerivedAppendOp`. Pass `pool` and `symbol_formids` to the new overloaded constructors.

The `Evaluator::evaluate_rule` path gives the planner access to `symbol_formids_` already. Thread it through.

- [ ] **Step 4: `is_simple_arg_expr` relaxes**

Instead of a whitelist, inverted: accept everything EXCEPT `Effect`/`ConditionalEffect` (those aren't Expr kinds anyway). Actually the cleanest move: extend the whitelist to include `BinaryExpr`, `CallExpr`, `FieldAccessExpr`, `EditorIdExpr`. For each, `spec_from_expr` falls through to `Kind::Expr` with the Expr pointer stored.

Restructure:

```cpp
EffectArgSpec spec_from_expr(const Expr& e, StringPool& pool,
                              const std::unordered_map<uint32_t, uint32_t>& symbols) {
    EffectArgSpec s{};
    if (auto const* ve = std::get_if<VariableExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Var;
        s.var_name = ve->name;
        return s;
    }
    // Precomputable constants: literals, symbols, editor IDs.
    if (auto const* il = std::get_if<IntLiteral>(&e.data))    { s.kind = EffectArgSpec::Kind::Constant; s.constant = Value::make_int(il->value); return s; }
    if (auto const* fl = std::get_if<FloatLiteral>(&e.data))  { s.kind = EffectArgSpec::Kind::Constant; s.constant = Value::make_float(fl->value); return s; }
    if (auto const* sl = std::get_if<StringLiteral>(&e.data)) { s.kind = EffectArgSpec::Kind::Constant; s.constant = Value::make_string(sl->value); return s; }
    if (auto const* kl = std::get_if<KeywordLiteral>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        auto it = symbols.find(kl->value.index);
        s.constant = (it != symbols.end()) ? Value::make_formid(it->second)
                                             : Value::make_keyword(kl->value);
        return s;
    }
    if (auto const* se = std::get_if<SymbolExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        auto it = symbols.find(se->name.index);
        s.constant = (it != symbols.end()) ? Value::make_formid(it->second)
                                             : Value::make_var();
        return s;
    }
    if (auto const* eid = std::get_if<EditorIdExpr>(&e.data)) {
        s.kind = EffectArgSpec::Kind::Constant;
        auto it = symbols.find(eid->id.index);
        s.constant = (it != symbols.end()) ? Value::make_formid(it->second)
                                             : Value::make_var();
        return s;
    }
    // Anything else: per-row expression evaluation.
    s.kind = EffectArgSpec::Kind::Expr;
    s.expr = &e;
    return s;
}
```

Drop `is_simple_arg_expr` — the planner now accepts all Expr kinds for effect args.

- [ ] **Step 5: Build + verify**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

Expected: all tests pass. New integration test (Task 1.6) covers BinaryExpr-in-effect-arg.

### Task 1.3: Multi-value `InClause` literal sets (`?x in [:A, :B, ...]`)

**Files:**
- Modify: `src/eval/op_in_clause.cpp`
- Modify: `src/eval/rule_planner.cpp`

The tuple-path InClause at `src/eval/evaluator.cpp:~229+` has a multi-value branch (when `c.values.size() > 1`, treat as a literal set, check membership). The vectorized planner currently falls back for this.

- [ ] **Step 1: Extend `InClauseOp::build_membership` to accept a vector of values**

```cpp
// Membership form: per row, check if the bound var is in a pre-computed
// set of Values (resolved once at planning time when possible, per-row
// when expressions are involved).
static std::unique_ptr<InClauseOp> build_membership(
    std::unique_ptr<Operator>   input,
    StringId                     var_name,
    std::vector<const mora::Expr*>  value_exprs,
    StringPool&                  pool,
    const std::unordered_map<uint32_t, uint32_t>& symbols);
```

Change from single-values-expr to vector-of-value-exprs. Internal `next_chunk` resolves each value expression per-row (simple: bind the row's cells into a Bindings map, evaluate each expr).

Alternatively: at build time, if every value expr is a pre-computable constant (literal / symbol / editor-id), pre-resolve once and compare Values directly (faster). Expression-valued set members are rare; the simple per-row implementation is acceptable.

For MVP in Plan 15: pre-resolve constants eagerly, evaluate non-constant exprs per-row. Worry about perf later if profiling demands.

- [ ] **Step 2: Planner routes multi-value InClause to the new membership builder**

In `rule_planner.cpp`, when handling an `InClause`, check:
- `values.size() == 1` and var is unbound → generator (Plan 14 shape)
- `values.size() == 1` and var is bound → single-value membership (Plan 14 shape — existing path)
- `values.size() > 1` → multi-value membership (new M1 path)

Zero-value InClause (`?x in []`) → empty-set membership — always false. Emit a short-circuit path that produces no rows.

- [ ] **Step 3: Test**

Integration test: `valid(?x) :- src(?x), ?x in [:A, :B]`. Seed relations, assert output.

### Task 1.4: `InClause`-first with unbound generator var

**Files:**
- Modify: `src/eval/rule_planner.cpp`
- Optional: `include/mora/eval/op_seed.h` + `src/eval/op_seed.cpp` — a "one-row empty seed" operator

Today the planner rejects rules whose first body clause is an `InClause` with an unbound generator var, because there's no upstream operator to supply rows.

- [ ] **Step 1: Option A — Introduce a `SeedOp` that emits one empty-row chunk**

```cpp
// Emits a single BindingChunk with 0 columns and 1 row (the empty
// tuple). Used to seed operator trees whose first clause is a
// generator (e.g. InClause-first, iterate-a-list-constant-then-do-...).
class SeedOp : public Operator {
public:
    SeedOp();
    std::optional<BindingChunk> next_chunk() override;
    const std::vector<StringId>& output_var_names() const override;
private:
    bool emitted_ = false;
    std::vector<StringId> empty_names_;
};
```

Implementation: `next_chunk` emits the single empty chunk on first call, returns `nullopt` thereafter.

- [ ] **Step 2: Planner uses `SeedOp` when needed**

In `plan_body`, if the first body clause is an InClause with a generator var, construct a `SeedOp` as the cumulative base. The InClause then scans the seed chunk (1 empty row), evaluates the values expr (constant or symbol-resolved), iterates the list, and emits one row per list element.

- [ ] **Step 3: Test**

Integration test for a rule like `result(?x) :- ?x in [:A, :B]` — no positive FactPattern, pure generation.

If this rule shape is uncommon enough, Option B is to continue rejecting it (fall back) — but since Plan 15 deletes the fallback, that path is no longer viable. Must support or document why the test corpus lacks this shape.

Before diving in: grep the test corpus for rules starting with an InClause. If zero results, document and skip SeedOp — add it only if a real rule needs it.

### Task 1.5: Naive fixpoint iteration for recursive derived rules

**Files:**
- Modify: `src/eval/evaluator.cpp`

Today `evaluate_module` runs each rule once in module order. For a recursive rule like `ancestor(?x, ?z) :- ancestor(?x, ?y), parent(?y, ?z)`, one pass won't produce all derived facts.

Simplest fix: after all rules run, check if `derived_facts_` grew during the module. If yes, re-run. Iterate until no growth.

- [ ] **Step 1: Measure derived_facts size before/after**

```cpp
void Evaluator::evaluate_module(const Module& mod, FactDB& out_facts,
                                  ProgressCallback progress) {
    ensure_effect_relations_configured(out_facts);

    constexpr size_t kMaxIterations = 64;
    for (size_t iter = 0; iter < kMaxIterations; ++iter) {
        size_t const before = derived_facts_.fact_count();
        for (size_t i = 0; i < mod.rules.size(); ++i) {
            const Rule& rule = mod.rules[i];
            evaluate_rule(rule, out_facts);
            if (progress) progress(i + 1, mod.rules.size(), pool_.get(rule.name));
        }
        size_t const after = derived_facts_.fact_count();
        if (after == before) break;  // fixpoint reached
    }
}
```

Drawback: each iteration re-emits identical effect facts (redundant writes that `add_fact` accepts as duplicates — FactDB is set semantics, so duplicates are absorbed). Acceptable for correctness.

Proper seminaive would only re-evaluate rules whose dependencies grew in the last iteration. That's Plan 16+ optimization.

- [ ] **Step 2: Test**

If the corpus has no recursive rules, add a synthetic test: seed two `parent` facts, define `ancestor` with a recursive rule, run, assert the transitive closure is computed.

- [ ] **Step 3: Caveat — idempotency of effect emission**

Effect relations (skyrim/set et al.) accept duplicates but FactDB's ColumnarRelation is a bag, not a set at the storage level (verify — if duplicates actually accumulate, we'd get N copies per iteration). Check `ColumnarRelation::append` — does it dedup? If not, and a rule's effect fires on iteration 1 AND iteration 2, we emit 2 copies.

If this is a real issue, mitigate by:
- (a) Only run rules whose body references relations that grew — not simple enough for M1
- (b) Track emitted effect tuples and skip duplicates — adds per-emit overhead
- (c) Deduplicate skyrim/* relations after evaluation — simplest but forces a full dedup pass

Start with the simplest: just iterate. Verify byte-identical parquet. If duplicates appear, add a post-pass dedup for `skyrim/*` relations. Document the choice in the commit.

### Task 1.6: Verify `vectorized_rules_count() == total_rules` on every fixture

**Files:**
- Modify: one of the integration tests (or add a new one)

- [ ] **Step 1: Add a full-coverage assertion test**

```cpp
TEST(VectorizedCoverage, AllExampleRulesVectorize) {
    // Run test_data/example.mora through the full compile pipeline.
    // Assert Evaluator::vectorized_rules_count() == rule count.
    // (Re-use the parse/resolve idiom from existing tests.)
}
```

- [ ] **Step 2: Run the CLI + assert**

```
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p15-m1 --sink parquet.snapshot=/tmp/mora-p15-m1/out
```

Add a `--verbose-eval` or similar debug flag to the CLI that reports `vectorized_rules_count / total_rules` in the summary table. OR just rely on the in-test assertion above — no need to expose it in the CLI.

- [ ] **Step 3: Expand the check to every fixture**

Walk every `.mora` file under `test_data/` and Skyrim extension fixtures. For each, parse+evaluate and assert 100% vectorization. The implementer flags any fixture that fails the check — either close the gap or investigate why the rule shape is still unsupported.

### Task 1.7: M1 commit

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

Expected: all tests pass. `test_cli_parquet_sink` 4/4.

- [ ] **Step 2: Byte-identical parquet**

Compare against Plan 14's output:

```bash
# HEAD = 7a2268e before M1 patches
git stash  # if you have in-progress work
git checkout 7a2268e
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p14 --sink parquet.snapshot=/tmp/mora-p14/out
git checkout mora-v3-foundation
git stash pop
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p15-m1 --sink parquet.snapshot=/tmp/mora-p15-m1/out
diff -r /tmp/mora-p14/out /tmp/mora-p15-m1/out
```

Expected: no diffs. If there are diffs, investigate — M1 promoted rules from fallback to vectorized, and the output must match.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: close vectorized gaps — EditorIdExpr, Expr effect args, multi-value InClause, naive fixpoint

Closes the last four shape gaps that routed rules through the tuple
fallback:

- EditorIdExpr in pattern args (op_scan) and effect args (spec_from_expr)
  resolves through symbol_formids, mirroring SymbolExpr.
- Effect args can now be arbitrary expressions — EffectArgSpec gains
  Kind::Expr, EffectAppendOp/DerivedAppendOp build per-row Bindings and
  evaluate via the free-function resolve_expr.
- Multi-value InClause literal sets (`?x in [:A, :B, ...]`) supported via
  InClauseOp::build_membership(vector<Expr*>).
- InClause-first with an unbound generator var supported via SeedOp
  (if any test fixture needed it; skipped otherwise).
- Evaluator::evaluate_module now iterates to fixpoint — runs all rules,
  checks for derived_facts growth, re-runs until stable (cap 64 iters).
  Naive; seminaive optimization deferred.

vectorized_rules_count() now equals the total rule count for every
test_data/ fixture. Byte-identical parquet output preserved. Plan 15
M2 deletes the tuple fallback.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — Delete the tuple fallback

All rule shapes now vectorize. Time to delete.

### Task 2.1: Remove the fallback branch from `Evaluator::evaluate_rule`

**File:** `src/eval/evaluator.cpp`

- [ ] **Step 1: Simplify `evaluate_rule`**

```cpp
void Evaluator::evaluate_rule(const Rule& rule, FactDB& db) {
    auto plan = plan_rule(rule, db_, derived_facts_, pool_, symbol_formids_);
    if (!plan) {
        diags_.error("eval-unsupported",
                      std::string("internal: vectorized planner declined rule '") +
                          std::string(pool_.get(rule.name)) + "'",
                      rule.span, source_line(rule.span));
        return;
    }
    for (auto& op : plan->effect_ops) op->run(db);
    if (plan->derived_op) plan->derived_op->run(derived_facts_);
}
```

After M1, the planner succeeds for every rule in the corpus. If anything's ever rejected, it's a bug — emit a clear diagnostic instead of silently falling back.

- [ ] **Step 2: Delete the `vectorized_rules_count_` counter + getter**

Remove from both `evaluator.h` and `evaluator.cpp`.

### Task 2.2: Delete `match_clauses`, `match_fact_pattern`, `merged_query`, `compute_clause_order`

**File:** `src/eval/evaluator.cpp` + `include/mora/eval/evaluator.h`

- [ ] **Step 1: Delete `match_clauses` and helpers**

~200 LOC block starting at `src/eval/evaluator.cpp:~132`. Also remove the private declaration in the header.

- [ ] **Step 2: Delete `match_fact_pattern`**

~100 LOC block starting at `src/eval/evaluator.cpp:~255`. Header too.

- [ ] **Step 3: Delete `merged_query`**

Small helper. Header too.

- [ ] **Step 4: Delete `compute_clause_order`**

~60 LOC block starting at `src/eval/evaluator.cpp:~65`. Header too.

- [ ] **Step 5: Delete the `Evaluator::resolve_expr` + `Evaluator::evaluate_guard` wrappers**

These were kept in Plan 14 M2 for tuple-path compatibility. Now the tuple path is gone. Delete the member functions. Callers already use the free-function versions from `expr_eval.h` — grep to confirm.

- [ ] **Step 6: Delete unused includes**

`evaluator.h` probably has `#include "mora/eval/field_types.h"` for the old `apply_effects` — check if still needed. Clean up.

### Task 2.3: Update tests that referenced the fallback

Tests like `test_planner_m1.cpp`, `test_rule_planner_single.cpp`, `test_rule_planner_multi.cpp`, `test_planner_guards.cpp` have cases that assert `vectorized_rules_count() == 0` (to prove a rule fell back) or `== 1` (to prove it vectorized). All such assertions are now meaningless — every rule vectorizes.

- [ ] **Step 1: Grep for `vectorized_rules_count`**

```
grep -rn 'vectorized_rules_count' tests
```

- [ ] **Step 2: Remove those assertions**

For each test case that asserted the counter:
- If the test's subject was "rule X vectorizes" → keep the test, drop the counter assertion (the test's value-level assertions still cover behavior).
- If the test's subject was "rule X falls back" → delete the test or convert to "rule X evaluates correctly" without caring about path.

### Task 2.4: Full build + test + CLI smoke

- [ ] **Step 1: Full test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

Expected: all tests pass. Total count should be similar to Plan 14 M3's 85 (a few tests deleted, a few tests simplified).

- [ ] **Step 2: Byte-identical parquet**

Compare against Plan 14 M3. Zero diffs.

```
xmake run test_cli_parquet_sink 2>&1 | tail -5
```

4/4 must pass.

- [ ] **Step 3: CLI smoke**

```
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p15-m2 --sink parquet.snapshot=/tmp/mora-p15-m2/out
```

Exit 0, parquet output present.

### Task 2.5: M2 commit

- [ ] **Step 1: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: delete tuple evaluator fallback — vectorized is the only path

With Plan 15 M1 closing every remaining vectorized gap, the tuple-based
fallback is dead code. Deletes:

- Evaluator::match_clauses (~200 LOC) — recursive tuple-row binding
- Evaluator::match_fact_pattern (~100 LOC) — tuple-based pattern query
- Evaluator::merged_query — helper for the above
- Evaluator::compute_clause_order (~60 LOC) — selectivity scoring heuristic
  (the vectorized planner's clause ordering lives in rule_planner.cpp)
- Evaluator::resolve_expr + evaluate_guard member wrappers (callers use
  the free-function versions from expr_eval.h directly)
- The vectorized_rules_count_ side-channel counter + public getter
- Fallback branch in Evaluator::evaluate_rule — failure is now a
  hard diagnostic ("internal: vectorized planner declined rule X")

~400 LOC deleted. Byte-identical parquet output preserved — every rule
that used to flow through match_clauses now flows through the operator
pipeline and produces the same facts.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 15)

1. `xmake test` all green.
2. `test_cli_parquet_sink` 4/4.
3. CLI smoke on `test_data/example.mora` exit 0 with parquet output byte-identical to Plan 14.
4. `grep -rn 'match_clauses\|match_fact_pattern\|merged_query\|compute_clause_order\|vectorized_rules_count' src include tests` empty except for this plan file.
5. `Evaluator::evaluate_rule` has no fallback branch — planner success is mandatory.
6. Branch is 35 commits above master (33 + 2 Plan 15 commits).

## Forward-looking for Plan 16

- **Drop verb keywords from the grammar.** `set_*`/`add_*`/`remove_*`/`mul_*` keywords disappear. Rules become `=>` pure-datalog: write to output relations directly via `skyrim/set(X, :Field, V)` etc. Updates: lexer, parser, grammar tests, every `.mora` fixture file.
- Many internal structures that carry `VerbKind`/`Effect`/`action_to_field` can go away. Planner's "effect-rel mapping via field_op_to_rel" gets simpler — the output relation is written in the `.mora` source directly.
- Plan 14/15 `spec_from_expr` + `EffectAppendOp` collapse into a simple "append a tuple to the named output relation" path.

## Potential post-v3 polish (neither in P15 nor P16)

- **Seminaive fixpoint.** Plan 15 M1 uses naive iteration. For corpora without recursive rules it's equivalent to a single pass; recursion is rare in Skyrim compile-time rules.
- **Vectorized `resolve_expr`**. `FilterOp` and the new `EffectArgSpec::Kind::Expr` path still construct per-row `Bindings` maps. A chunk-wide expression evaluator (operate on whole columns) would be faster.
- **Arrow zero-copy sink** — flagged back in Plan 12; parquet sink still walks cells one at a time.
- **Ctor assertions → hard errors** — `UnionOp`/`AntiJoinOp`/`InClauseOp` use `assert()` for planner-bug invariants. Promote to exceptions in release builds.

## Critical files

- `/home/tbaldrid/oss/mora/src/eval/op_scan.cpp` — **modified** (EditorIdExpr)
- `/home/tbaldrid/oss/mora/src/eval/rule_planner.cpp` — **heavily modified** across M1 (spec_from_expr, InClause multi-value)
- `/home/tbaldrid/oss/mora/include/mora/eval/op_append.h` + `src/eval/op_append.cpp` — **modified** (EffectArgSpec::Kind::Expr + per-row resolve)
- `/home/tbaldrid/oss/mora/src/eval/op_in_clause.cpp` — **modified** (multi-value set + seed support)
- `/home/tbaldrid/oss/mora/include/mora/eval/op_seed.h` + `src/eval/op_seed.cpp` — **new** if InClause-first is in the corpus
- `/home/tbaldrid/oss/mora/src/eval/evaluator.cpp` — **heavily modified** in M2 (delete tuple path + naive fixpoint)
- `/home/tbaldrid/oss/mora/include/mora/eval/evaluator.h` — **modified** (shrink API)
