# Plan 16 — Drop verb keywords; unify on pure datalog rule heads

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `set_*` / `add_*` / `remove_*` / `mul_*` verb keywords from the grammar. Rules with side effects write to output relations via qualified rule heads: a rule like

```
tag_bandits(NPC) :- form/npc(NPC), form/faction(NPC, @BanditFaction) => add form/keyword(NPC, @BanditMarker).
```

becomes

```
skyrim/add(NPC, :Keyword, @BanditMarker) :- form/npc(NPC), form/faction(NPC, @BanditFaction).
```

No more `Effect` or `ConditionalEffect` AST nodes. No more `VerbKind`, `action_to_field`, `verb_prefix`, `EffectAppendOp`, or `FieldId`/`FieldOp`. The planner has one code path — every rule is a derived rule that appends tuples to its head relation.

**Architecture:** The grammar gains **qualified rule heads** — a rule head can be `ns/name(args)` (same shape as FactPattern references to external relations). The parser's `=>` effect syntax goes away; rules are always `head :- body.`. The Rule AST grows a `StringId head_qualifier` field. The planner's `plan_rule` collapses: no effect_ops, no conditional_effects — just build a body tree, then `DerivedAppendOp` writes to the head relation.

Effect-relation configuration (`skyrim/{set,add,remove,multiply}`) moves from `Evaluator::ensure_effect_relations_configured` into Skyrim's `register_skyrim` (the extension owns its output relations' schemas). Evaluator's role reduces further.

**Tech Stack:** C++20, xmake, gtest.

**Branch:** `mora-v3-foundation`
**Base:** `49150f4` (HEAD after Plan 15 + polish, 36 commits above master)

**Non-negotiable invariants:**
1. **Byte-identical parquet output** for the rewritten test corpus. Every `.mora` fixture is migrated to the new syntax; after migration, running through the evaluator must produce identical effect facts to Plan 15's output.
2. `test_cli_parquet_sink` 4/4 across both milestones.

**Scope note.** After Plan 16, the v3 rewrite is complete. Optional post-rewrite polish (Arrow zero-copy, seminaive, vectorized expression eval) remains available as future work.

---

## Milestone 1 — New grammar + parser + fixture migration + planner unification

The grammar change, AST change, parser rewrite, and fixture migration happen atomically. After M1, the codebase builds with the new grammar; the old verb-handling code still exists but is unused and unreferenced by any source — M2 sweeps it.

### Task 1.1: AST — Rule gains qualified head; drop Effect + ConditionalEffect

**Files:**
- Modify: `include/mora/ast/ast.h`

- [ ] **Step 1: Update `Rule`**

Current (approximate):

```cpp
struct Rule {
    StringId                            name;
    std::vector<Expr>                   head_args;
    std::vector<Clause>                 body;
    std::vector<Effect>                 effects;
    std::vector<ConditionalEffect>      conditional_effects;
    SourceSpan                          span;
};
```

Replace with:

```cpp
struct Rule {
    StringId            qualifier;   // "skyrim", "form", or empty for user rules
    StringId            name;        // "set", "add", "bandit", "tag_bandits", ...
    std::vector<Expr>   head_args;
    std::vector<Clause> body;
    SourceSpan          span;
};
```

- [ ] **Step 2: Delete `Effect`, `ConditionalEffect`, `VerbKind`**

All three AST types are deleted. The `body` clause variants that remain: `FactPattern`, `GuardClause`, `InClause`. `Effect` and `ConditionalEffect` were only body clause variants in some older AST revisions; check — if they're already standalone types, just delete them.

Update `Clause`'s `std::variant<...>` to drop `Effect`/`ConditionalEffect` if they were ever members.

- [ ] **Step 3: Remove `#include` directives that only existed for these types**

### Task 1.2: Parser — qualified rule heads; reject verb syntax

**Files:**
- Modify: `src/parser/parser.cpp`
- Read only: `src/lexer/lexer.cpp` (no changes expected — `/` is already tokenized for namespace refs in body clauses)

- [ ] **Step 1: Parse qualified rule heads**

Where the parser currently parses `rule_name(args)` as a rule head, extend to accept `ns/rule_name(args)`. The lexer should already emit `Slash` between identifiers (used in body FactPatterns). Reuse that logic.

The rule-head parser's shape (approximate — adjust to existing code):

```cpp
// Before:
StringId name = parse_ident();
auto args = parse_head_args();

// After:
StringId first = parse_ident();
StringId qualifier = {};
StringId name = first;
if (match(TokenKind::Slash)) {
    qualifier = first;
    name = parse_ident();
}
auto args = parse_head_args();
```

- [ ] **Step 2: Delete `=>` effect-syntax parsing**

Find the parser branch that handles the `=>` token and reads `set_*` / `add_*` etc. keywords. Delete that branch. The `=>` token itself can stay as a parse error (unexpected token) or be removed from the lexer.

If `=>` is only used in effect syntax, remove it from the lexer as well.

- [ ] **Step 3: Delete `parse_effect`, `parse_conditional_effect`, `parse_verb`, and any `VerbKind` references**

These are helper functions that produce the deleted AST types. Remove them entirely.

- [ ] **Step 4: Build**

```
xmake build mora_lib 2>&1 | tail -10
```

Expected: compile failures elsewhere (evaluator, planner, tests) — they reference the deleted AST. Those fixes come in Task 1.3 + 1.4. For now, just confirm the parser itself compiles cleanly if you build just the parser-relevant files.

### Task 1.3: Planner — one code path (no effect_ops)

**Files:**
- Modify: `src/eval/rule_planner.cpp`
- Modify: `include/mora/eval/rule_planner.h`

- [ ] **Step 1: Simplify `RulePlan`**

```cpp
struct RulePlan {
    std::unique_ptr<DerivedAppendOp> append_op;
};
```

No more `effect_ops` vector. Every rule has exactly one append sink.

- [ ] **Step 2: Rewrite `plan_rule`**

```cpp
std::optional<RulePlan> plan_rule(
    const Rule&                                   rule,
    const FactDB&                                 input_db,
    const FactDB&                                 derived_facts,
    StringPool&                                   pool,
    const std::unordered_map<uint32_t, uint32_t>& symbol_formids)
{
    auto body = plan_body(rule, input_db, derived_facts, pool, symbol_formids);
    if (!body) return std::nullopt;

    // All head args must be simple specs that resolve per-row.
    std::vector<EffectArgSpec> head_specs;
    head_specs.reserve(rule.head_args.size());
    for (const Expr& ha : rule.head_args) {
        head_specs.push_back(spec_from_expr(ha, pool, symbol_formids));
    }

    // Determine the target relation name. Qualified → "ns/name"; else
    // just "name".
    StringId head_rel;
    if (rule.qualifier.index != 0) {
        std::string qn;
        qn += pool.get(rule.qualifier);
        qn += '/';
        qn += pool.get(rule.name);
        head_rel = pool.intern(qn);
    } else {
        head_rel = rule.name;
    }

    RulePlan plan;
    plan.append_op = std::make_unique<DerivedAppendOp>(
        std::move(*body), head_rel, std::move(head_specs),
        pool, symbol_formids);
    return plan;
}
```

- [ ] **Step 3: Delete effect-specific planner code**

Remove: `field_op_to_rel`, `verb_prefix`, the legacy-action-name synthesis, the effect-arg validation, the conditional-effect loop, the entire effects branch.

- [ ] **Step 4: Rename / simplify `EffectArgSpec`**

Since there are no more "effect args" distinct from head args, rename `EffectArgSpec` → `HeadArgSpec`. Internals unchanged.

- [ ] **Step 5: `EffectAppendOp` merges into `DerivedAppendOp`**

`EffectAppendOp` was a specialized three-column writer (formid, keyword, value). `DerivedAppendOp` is the general N-column writer. Both write via `FactDB::add_fact` — they're structurally identical except for arity. Delete `EffectAppendOp` (the class and file); callers that constructed it now construct `DerivedAppendOp` with 3 head specs. The rule `skyrim/set(NPC, :GoldValue, 100)` compiles to a 3-arg `DerivedAppendOp` targeting `skyrim/set` — exactly what EffectAppendOp was doing, now uniformly expressed.

Files to delete after M2: `include/mora/eval/op_append.h`'s `EffectAppendOp` declaration, `src/eval/op_append.cpp`'s impl. Keep `DerivedAppendOp`.

### Task 1.4: Evaluator — unify the run path

**Files:**
- Modify: `src/eval/evaluator.cpp`
- Modify: `include/mora/eval/evaluator.h`

- [ ] **Step 1: Simplify `evaluate_rule`**

```cpp
void Evaluator::evaluate_rule(const Rule& rule, FactDB& db) {
    auto plan = plan_rule(rule, db_, derived_facts_, pool_, symbol_formids_);
    if (!plan) {
        std::string const src_line = current_module_
            ? current_module_->get_line(rule.span.start_line)
            : std::string{};
        diags_.error("eval-unsupported",
                      std::string("internal: vectorized planner declined rule '") +
                          std::string(pool_.get(rule.name)) + "'",
                      rule.span, src_line);
        return;
    }
    // Skyrim output relations (qualifier == "skyrim") write directly to db.
    // User-defined rules (unqualified or other qualifier) write to derived_facts_.
    FactDB& target = (rule.qualifier.index != 0 &&
                      pool_.get(rule.qualifier) == "skyrim")
                     ? db
                     : derived_facts_;
    plan->append_op->run(target);
}
```

- [ ] **Step 2: Move effect-relation configuration to the Skyrim extension**

Delete `ensure_effect_relations_configured` from Evaluator. Instead, in `extensions/skyrim_compile/src/register.cpp`, the `register_skyrim` function already registers `skyrim/{set,add,remove,multiply}` schemas via the ext_ctx. After M1, `FactDB::configure_relation` gets called for these four when `ExtensionContext::load_required` runs — which happens before evaluation. Verify by tracing.

If the existing extension registration doesn't call `configure_relation` on FactDB for these relations, add it as part of `register_skyrim` or the data-source load path.

- [ ] **Step 3: Delete `effect_rel_*_`, `effect_rels_configured_` members**

Evaluator loses its hardcoded effect-relation names. The planner's `head_rel` string matches whatever the rule's head says — no special-casing.

### Task 1.5: Fixture migration — rewrite all `.mora` files

**Files:**
- Modify: every `.mora` file under `test_data/` and `extensions/skyrim_compile/tests/fixtures/`

- [ ] **Step 1: Inventory the .mora corpus**

```
find test_data extensions -name '*.mora' | xargs wc -l
```

List every file and its rule count.

- [ ] **Step 2: Rewrite each file**

For each rule with an `=>` effect, convert to a qualified-head rule. Examples:

```
# Before
tag_bandits(NPC) :- form/npc(NPC), form/faction(NPC, @BanditFaction) => add form/keyword(NPC, @BanditMarker).

# After
skyrim/add(NPC, :Keyword, @BanditMarker) :- form/npc(NPC), form/faction(NPC, @BanditFaction).
```

For a set verb with a scalar field like `gold_value`:

```
# Before
elite_bandits(NPC) :- form/npc(NPC), Level >= 20 => set gold_value(NPC, 100).

# After
skyrim/set(NPC, :GoldValue, 100) :- form/npc(NPC), Level >= 20.
```

For conditional effects:

```
# Before
r(NPC) :- form/npc(NPC) => set gold_value(NPC, 100) if Level >= 10.

# After (guard moves into body)
skyrim/set(NPC, :GoldValue, 100) :- form/npc(NPC), Level >= 10.
```

For multi-effect rules, split into one rule per effect.

Cross-check the field-name-to-keyword mapping: the old `set_gold_value` maps to `FieldId::GoldValue` maps to the keyword `:GoldValue`. Every `.mora` file needs the correct keyword. Reference `src/data/action_names.h` (before deletion) + `include/mora/model/field_names.h` to get the canonical names.

- [ ] **Step 3: Special-case — `bandit_bounty.mora`**

Plan 15 M2 gated this fixture because `add_gold` maps to `FieldId::Invalid`. In Plan 16's new world, `add_gold` would become `skyrim/add(?x, :Gold, ?amt)` — but does `:Gold` correspond to any real field? Check `field_names.h`. If yes, the rule migrates cleanly. If no, the rule references a phantom field and can be deleted or converted to target a different output relation.

Un-gate the fixture in `test_vectorized_coverage.cpp` if M1 makes it valid; otherwise keep the gate with a clearer comment (e.g. "uses :Gold which has no field mapping — domain gap, not vectorizer gap").

### Task 1.6: Build + tests

- [ ] **Step 1: Full build**

```
xmake build 2>&1 | tail -15
```

Expected: clean. All files compile against the new AST.

- [ ] **Step 2: Full test**

```
xmake test 2>&1 | tail -5
```

- [ ] **Step 3: Byte-identical parquet**

Compare against Plan 15 output:

```bash
git stash
git checkout 49150f4
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p15 --sink parquet.snapshot=/tmp/mora-p15/out
git checkout mora-v3-foundation
git stash pop
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p16-m1 --sink parquet.snapshot=/tmp/mora-p16-m1/out
diff -r /tmp/mora-p15/out /tmp/mora-p16-m1/out
```

Expected: no row diffs. Column order in parquet files may differ if the new path orders things differently — focus on logical content (same set of (formid, field, value) tuples per relation).

- [ ] **Step 4: Every test binary passes**

Tests that parsed `.mora` strings inline (e.g. `test_evaluator_effect_facts.cpp`) also migrate to the new syntax.

### Task 1.7: M1 commit

- [ ] **Step 1: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: drop verb keywords — qualified rule heads replace effect syntax

Rules with side effects now write to output relations via qualified
heads instead of the `=> verb name(...)` effect syntax. A rule like

  tag_bandits(NPC) :- form/npc(NPC), form/faction(NPC, @BanditFaction)
    => add form/keyword(NPC, @BanditMarker).

becomes

  skyrim/add(NPC, :Keyword, @BanditMarker)
    :- form/npc(NPC), form/faction(NPC, @BanditFaction).

Grammar changes:
- Rule heads accept `namespace/name(args)` syntax.
- `=>` effect syntax deleted from the parser.
- AST drops Effect, ConditionalEffect, VerbKind. Rule gains a
  `qualifier: StringId` field alongside `name`.
- Conditional effects become guards in the rule body.

Planner unified: every rule is a derived rule that appends via
DerivedAppendOp to its head relation. EffectAppendOp folded in (now
just a 3-column DerivedAppendOp for skyrim/*-qualified heads).
Evaluator::evaluate_rule picks the target FactDB (output db for
skyrim/* heads; derived_facts_ for user rules).

Every .mora fixture in test_data/ and extensions/ migrated to the new
syntax. Byte-identical effect-fact output on test_data/example.mora.

Plan 16 M2 sweeps the now-unused verb-handling code (action_to_field,
verb_prefix, EffectAppendOp class, FieldId/FieldOp enums if they
become dead, effect-relation configuration on Evaluator).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 2 — Delete unused verb-handling code

After M1, `action_to_field`, `verb_prefix`, `EffectAppendOp`, `FieldId`/`FieldOp` enums, and related helpers are unreferenced. Delete them.

### Task 2.1: Delete `EffectAppendOp`

**Files:**
- Modify: `include/mora/eval/op_append.h` — remove the `EffectAppendOp` class
- Modify: `src/eval/op_append.cpp` — remove its implementation

- [ ] **Step 1: Grep for remaining references**

```
grep -rn 'EffectAppendOp' src include tests extensions
```

Expected: empty after M1. If anything persists, fix in M1 before deleting.

- [ ] **Step 2: Delete**

Leave `DerivedAppendOp` intact; it's the surviving class.

### Task 2.2: Delete `action_to_field` + `verb_prefix`

**Files:**
- Modify: `include/mora/data/action_names.h` — check if the whole file can be deleted or just parts
- Modify: `src/data/action_names.cpp` — delete if file only hosted these helpers

- [ ] **Step 1: Grep for references**

```
grep -rn 'action_to_field\|verb_prefix' src include tests extensions
```

Expected: empty.

- [ ] **Step 2: Delete the symbols + containing files if empty**

If `action_names.h/cpp` only hosted these two helpers, delete both files entirely. Update `xmake.lua`'s `src/data/*.cpp` file list.

### Task 2.3: Delete `FieldId` / `FieldOp` if unused

**Files:**
- Read-only check: `include/mora/eval/field_types.h`
- Potentially modify/delete: `include/mora/eval/field_types.h`, `include/mora/model/field_names.h`

- [ ] **Step 1: Grep for remaining users**

```
grep -rn 'FieldId\|FieldOp' src include tests extensions
```

After M1, the planner no longer emits FieldIds — but other code might still reference them (e.g. `field_names.h`'s `field_id_name()` helper which takes a `FieldId` and returns a `std::string_view`).

If `field_id_name()` has no callers after M1, delete it (and the FieldId enum). If it's still used (e.g. by docs/tests), keep.

Similarly for FieldOp: if used only by the deleted planner effect path, delete.

- [ ] **Step 2: Delete or keep per grep results**

Be conservative — if something still uses these types post-M1, leave them alone. The goal isn't maximal deletion; it's deleting the verb path. Field enums are orthogonal.

### Task 2.4: Tests that used verb syntax

**Files:**
- Check: every `tests/**/*.cpp` and `extensions/skyrim_compile/tests/**/*.cpp` that constructs rules inline

After M1 migrated `.mora` fixtures, inline-constructed rules in test C++ files also need updating. Grep for the old syntax patterns (`=> set`, `=> add`, etc.) in `.cpp` files (not just `.mora`):

```
grep -rn ' => set\| => add\| => remove\| => mul' tests extensions
```

If any test-code strings use the old syntax, migrate them. (M1 might have already caught these.)

### Task 2.5: Verification

- [ ] **Step 1: Full build + test**

```
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -5
```

- [ ] **Step 2: `test_cli_parquet_sink`**

```
xmake run test_cli_parquet_sink 2>&1 | tail -5
```

4/4 pass.

- [ ] **Step 3: CLI smoke**

```
xmake run mora -- compile test_data/example.mora --output-dir /tmp/mora-p16-m2 --sink parquet.snapshot=/tmp/mora-p16-m2/out
echo "exit: $?"
```

Exit 0 with parquet output.

- [ ] **Step 4: Final grep sanity**

```
grep -rn 'VerbKind\|class Effect\|ConditionalEffect\|EffectAppendOp\|verb_prefix' src include tests extensions
```

All should be empty outside plan docs.

### Task 2.6: M2 commit

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: delete verb-handling code — EffectAppendOp, action_to_field, VerbKind

With Plan 16 M1 migrating all rules to qualified-head syntax, every
verb-handling symbol becomes dead weight. Deletes:

- EffectAppendOp class + impl — folded into DerivedAppendOp in M1
- action_to_field free function + src/data/action_names.h/cpp (if empty)
- verb_prefix helper
- FieldId / FieldOp enums (if no remaining users — kept otherwise)
- Evaluator's effect-relation configuration (now owned by the Skyrim
  extension)

Net deletion: ~XXX LOC. All grep-confirmed unreferenced.

v3 rewrite complete.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Verification (end of Plan 16)

1. `grep -rn 'VerbKind\|class Effect\|ConditionalEffect\|EffectAppendOp\|verb_prefix\|action_to_field' src include tests extensions` empty.
2. `xmake test` all green.
3. `test_cli_parquet_sink` 4/4.
4. CLI smoke on `test_data/example.mora` exits 0; parquet output logically identical to Plan 15 (same (formid, field, value) tuples).
5. Branch is 38 commits above master (36 + 2 Plan 16 commits).
6. Every `.mora` file uses qualified-head syntax; no `=>` anywhere except comments.
7. `Evaluator::ensure_effect_relations_configured` is gone; effect-relation schemas are registered by the Skyrim extension.

## Post-rewrite retrospective (end of v3)

With Plan 16 complete, the v3 rewrite is done. Major milestones in order:

- **Plans 1–4:** Extension API + ESP data source + parquet sink + relation registration. Skyrim moves from "baked into core" to "one extension among many."
- **Plans 5–7:** Effect relations (`skyrim/set` etc.) introduced; bridge from PatchSet to FactDB; typed keyword encoding.
- **Plan 8:** Evaluator writes effect facts directly into FactDB. Delete `PatchSet`, `PatchBuffer`, `src/emit/`, `mora_patches.bin`.
- **Plan 9:** New type system (`Type`, `TypeRegistry`, nominals). Delete `TypeChecker`, `TypeKind`, `MoraType`.
- **Plan 10:** Typed `Vector` + `Column` infrastructure.
- **Plan 11:** FactDB stores relations as columns. Delete `IndexedRelation`.
- **Plan 12:** Nominal decode via `Type::kind_hint()`; parquet sink reads columns.
- **Plan 13:** Vectorized evaluator MVP (positive-conjunction rules).
- **Plan 14:** Vectorized coverage widening (non-Set verbs, multi-effect, guards, negation, InClause).
- **Plan 15:** Close remaining gaps + delete tuple fallback.
- **Plan 16:** Drop verb keywords; unified derived-rule evaluator.

## Optional post-v3 polish

- **Arrow zero-copy parquet sink** (flagged Plan 12): sink walks cells one at a time via `column(c).at(r)`. Bulk `Int32Vector::data() → arrow::Int32Array` conversion would be significantly faster.
- **Seminaive fixpoint** (flagged Plans 13–15): current corpus has no recursive rules, so naive = single pass. Add delta-driven iteration when a recursive rule enters the corpus.
- **Vectorized expression evaluator** (flagged Plan 14): FilterOp + `HeadArgSpec::Kind::Expr` construct per-row Bindings. A chunk-wide column evaluator would be faster.
- **Assertion → exception promotion** for planner-bug invariants in AntiJoinOp / UnionOp / InClauseOp ctors (flagged Plan 14).
- **`bandit_bounty.mora` domain gap** — add a `:Gold` field mapping if the model gains one, or delete the fixture.

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/ast/ast.h` — Rule + Clause restructure
- `/home/tbaldrid/oss/mora/src/parser/parser.cpp` — qualified rule heads, drop effect syntax
- `/home/tbaldrid/oss/mora/src/eval/rule_planner.cpp` — unified derived-rule path
- `/home/tbaldrid/oss/mora/src/eval/evaluator.cpp` — simplified evaluate_rule, drops effect-relation registration
- `/home/tbaldrid/oss/mora/include/mora/eval/op_append.h` — EffectAppendOp deleted
- `/home/tbaldrid/oss/mora/extensions/skyrim_compile/src/register.cpp` — may need to configure effect relations post-Evaluator-relinquishing
- Every `.mora` file in `test_data/` + `extensions/skyrim_compile/tests/fixtures/`
