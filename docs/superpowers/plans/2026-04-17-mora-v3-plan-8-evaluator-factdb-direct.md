# Plan 8 — Evaluator writes effect facts directly; retire PatchSet + src/emit/

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the evaluator produce effect-fact tuples straight into the FactDB output relations (`skyrim/set`, `skyrim/add`, `skyrim/remove`, `skyrim/multiply`). Delete the `PatchSet → ResolvedPatchSet → populate_effect_facts` bridge, the entire `src/emit/` binary-format subsystem, and `mora_patches.bin` generation. After this commit, parquet is the only snapshot format and the evaluator no longer owns a tuple-level side table.

**Architecture:** Replace `Evaluator::evaluate_static(Module) → PatchSet` with `Evaluator::evaluate_module(const Module&, FactDB&, ProgressCallback)`. `apply_effects` computes the FieldOp-to-relation-name mapping inline (`Set → "skyrim/set"`, etc.), interns the field name as a keyword, and calls `db.add_fact(rel_id, Tuple{formid, keyword, value})`. `cmd_compile` drops PatchBuffer / PatchSet / write_patch_file / populate_effect_facts entirely — it evaluates and then dispatches configured sinks. `cmd_inspect` switches from a PatchSet dump to a FactDB dump of the `skyrim/*` relations. Priority and "conflict detection" were artifacts of the legacy runtime-DLL patch-application model (already deleted in Plan 2) and are dropped with PatchSet.

**Tech Stack:** C++20, xmake, gtest. No new dependencies.

**Scope note.** This plan does NOT rewrite the evaluator vectorized. The existing tuple-by-tuple `Bindings` + `match_clauses` internals stay unchanged. The only delta is where output gets written — FactDB tuples, not a PatchSet. Vectorized rewrite is a later plan.

**Branch:** `mora-v3-foundation`
**Base:** `eaeff87` (HEAD, Plan 7)

---

### Task 1: Evaluator signature + apply_effects rewrite

**Files:**
- Modify: `include/mora/eval/evaluator.h`
- Modify: `src/eval/evaluator.cpp`

Change `Evaluator`'s public API to write directly into a `FactDB&` output parameter. `apply_effects`, `evaluate_rule`, `match_clauses` all thread that `FactDB&` through. `apply_effects` interns relation names once (cached in a member) and calls `db.add_fact(rel_id, Tuple{formid, field_keyword, value})` instead of `patches.add_patch(...)`.

- [ ] **Step 1: Update the header**

Replace `evaluate_static(const Module&, ProgressCallback)` with:

```cpp
void evaluate_module(const Module& mod, FactDB& out_facts,
                     ProgressCallback progress = nullptr);
```

Drop `#include "mora/eval/patch_set.h"`. Drop the `priority` parameter from `evaluate_rule`, `match_clauses`, and `apply_effects` — it has no consumer now. Change each `PatchSet&` parameter in the private helpers to `FactDB&`. Add four private members cached at first use:

```cpp
StringId effect_rel_set_;
StringId effect_rel_add_;
StringId effect_rel_remove_;
StringId effect_rel_multiply_;
bool     effect_rels_configured_ = false;
void ensure_effect_relations_configured(FactDB& db);
```

- [ ] **Step 2: Implement `ensure_effect_relations_configured`**

In `src/eval/evaluator.cpp`:

```cpp
void Evaluator::ensure_effect_relations_configured(FactDB& db) {
    if (effect_rels_configured_) return;
    effect_rel_set_      = pool_.intern("skyrim/set");
    effect_rel_add_      = pool_.intern("skyrim/add");
    effect_rel_remove_   = pool_.intern("skyrim/remove");
    effect_rel_multiply_ = pool_.intern("skyrim/multiply");
    for (StringId rel : {effect_rel_set_, effect_rel_add_,
                         effect_rel_remove_, effect_rel_multiply_}) {
        db.configure_relation(rel, /*arity*/ 3, /*indexed*/ {0});
    }
    effect_rels_configured_ = true;
}
```

- [ ] **Step 3: Rewrite `apply_effects`**

Replace the body of `apply_effects` with the same structure (unconditional + conditional effect loops, action_to_field lookup, LeveledEntries packing branch), but instead of `patches.add_patch(...)`, emit:

```cpp
auto emit_effect = [&](uint32_t formid, FieldId field, FieldOp op,
                       const Value& v) {
    StringId rel;
    switch (op) {
        case FieldOp::Set:      rel = effect_rel_set_;      break;
        case FieldOp::Add:      rel = effect_rel_add_;      break;
        case FieldOp::Remove:   rel = effect_rel_remove_;   break;
        case FieldOp::Multiply: rel = effect_rel_multiply_; break;
    }
    auto kw = Value::make_keyword(pool_.intern(field_id_name(field)));
    db.add_fact(rel, Tuple{Value::make_formid(formid), kw, v});
};
```

and call `emit_effect(target.as_formid(), field, op, value)` where the old code called `patches.add_patch(...)`. Drop `current_mod_name_` and `priority` arguments — `add_fact` takes none of that. Remove the `current_mod_name_` member (grep for other uses and clear them; it was only threaded through for `patches.add_patch`'s `source_mod` argument).

`evaluate_module` body:

```cpp
void Evaluator::evaluate_module(const Module& mod, FactDB& out_facts,
                                  ProgressCallback progress) {
    ensure_effect_relations_configured(out_facts);
    for (size_t i = 0; i < mod.rules.size(); ++i) {
        const Rule& rule = mod.rules[i];
        evaluate_rule(rule, out_facts);
        if (progress) progress(i + 1, mod.rules.size(), pool_.get(rule.name));
    }
}
```

Include `mora/model/field_names.h` for `field_id_name`.

- [ ] **Step 4: Build and confirm evaluator.cpp compiles standalone**

```
xmake build mora_lib 2>&1 | head -80
```

Expected: compile succeeds for `src/eval/evaluator.cpp`. Failures elsewhere (main.cpp still references old API) are expected at this step; just confirm evaluator.cpp itself is clean.

- [ ] **Step 5: Commit checkpoint (optional)**

Do NOT commit yet — Plan 8 is a single commit at the end. Just continue to the next task.

---

### Task 2: `cmd_compile` — strip PatchBuffer/PatchSet/mora_patches.bin

**Files:**
- Modify: `src/main.cpp`

Remove `evaluate_mora_rules`, `write_patch_file`, `build_static_arrangements_section`. Inline a minimal evaluator-drive loop in `cmd_compile`. Drop the `mora_patches.bin` summary line. The `--sink parquet.snapshot=...` path is unchanged — it already reads the FactDB.

- [ ] **Step 1: Delete `evaluate_mora_rules`, `write_patch_file`, `build_static_arrangements_section`**

Remove the three functions wholesale (roughly `src/main.cpp:336..482` plus any forward decls). They depended on `mora::PatchBuffer`, `mora::ResolvedPatchSet`, `mora::PatchEntry`, `mora::build_patch_entries_and_string_table`, `mora::emit::build_u32_arrangement`, `mora::emit::build_arrangements_section`, `mora::serialize_patch_table` — all vanish with `src/emit/` deletion in Task 4.

Remove these includes from the top of `src/main.cpp` (grep and delete matches):

```
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"
#include "mora/eval/effect_facts.h"
#include "mora/emit/patch_table.h"
#include "mora/emit/arrangement_emit.h"
#include "mora/emit/flat_file_writer.h"
#include "mora/emit/patch_file_v2.h"
```

Keep `mora/eval/evaluator.h` + `mora/eval/fact_db.h`.

- [ ] **Step 2: Replace the evaluation block in `cmd_compile`**

Find the current block (approximately `src/main.cpp:593..604`):

```cpp
mora::PatchBuffer patch_buf;
std::vector<uint8_t> string_table;
mora::ResolvedPatchSet mora_resolved;
evaluate_mora_rules(cr, evaluator, patch_buf, string_table,
                    mora_resolved, out);
mora::populate_effect_facts(mora_resolved, db, cr.pool);

out.phase_done(fmt::format("{} total patches", patch_buf.size()));

// Write patch file
int const write_rc = write_patch_file(patch_buf, string_table, target_path, output_dir, out, loaded_plugins, db, cr.pool);
if (write_rc != 0) return write_rc;
```

Replace with:

```cpp
if (!cr.modules.empty()) {
    out.phase_start("Evaluating (.mora rules)");
    auto eval_progress = [&](size_t current, size_t total,
                             [[maybe_unused]] std::string_view name) {
        out.progress_update(fmt::format("Evaluating rule {} / {} ...",
                                         current, total));
    };
    for (auto& mod : cr.modules) {
        evaluator.evaluate_module(mod, db, eval_progress);
    }
    out.progress_clear();
    out.phase_done("done");
}
```

The `output_dir` parameter is still used by sinks via `sink_configs`, so don't remove it.

- [ ] **Step 3: Update the compile-summary block**

Replace the `summary` vector that mentioned `mora_patches.bin` and `patch_buf.size()` with a FactDB-based summary. Find the block (approximately `src/main.cpp:626..648`):

```cpp
auto patch_path = fs::path(target_path);
if (fs::is_regular_file(patch_path)) patch_path = patch_path.parent_path();
patch_path = patch_path / output_dir / "mora_patches.bin";
auto patch_size = fs::exists(patch_path) ? fs::file_size(patch_path) : 0;

std::vector<mora::TableRow> summary;
summary.push_back({"Frozen:", fmt::format("{} rules", static_count),
    fmt::format("\xe2\x86\x92 mora_patches.bin ({})", format_bytes(patch_size))});
summary.push_back({"", fmt::format("{} patches baked into native code", patch_buf.size()), ""});
summary.push_back({"", "Estimated runtime: <15ms", ""});
```

Replace with:

```cpp
size_t effect_count = 0;
for (const char* rel : {"skyrim/set", "skyrim/add",
                         "skyrim/remove", "skyrim/multiply"}) {
    effect_count += db.fact_count(cr.pool.intern(rel));
}

std::vector<mora::TableRow> summary;
summary.push_back({"Frozen:", fmt::format("{} rules", static_count),
    fmt::format("\xe2\x86\x92 {} effect fact(s)", effect_count)});
```

Delete the dynamic-rules summary row only if `dynamic_count` is still being computed. (It is — keep that row.) The "baked into native code" and "Estimated runtime" rows go entirely.

- [ ] **Step 4: Build the CLI and confirm mora_patches.bin logic is gone**

```
xmake build mora 2>&1 | head -40
```

Expected: succeeds. Confirm no remaining reference to `mora_patches.bin` in `src/main.cpp`:

```
grep -n 'mora_patches\|PatchBuffer\|PatchSet\|write_patch_file\|populate_effect_facts' src/main.cpp
```

Expected output: empty.

---

### Task 3: `cmd_inspect` — dump FactDB skyrim/* relations

**Files:**
- Modify: `src/main.cpp` (the `cmd_inspect` function, roughly lines 660..742)

Replace PatchSet-based dump with a FactDB dump. Drop the `--show-conflicts` branch entirely (conflicts were a Skyrim-mod priority concept that's gone with PatchSet).

- [ ] **Step 1: Rewrite the evaluation block of `cmd_inspect`**

Replace (lines ~700..713):

```cpp
mora::FactDB const db(pool);
mora::Evaluator evaluator(pool, diags, db);
mora::PatchSet all_patches;
for (auto& mod : modules) {
    auto mod_patches = evaluator.evaluate_static(mod);
    auto resolved = mod_patches.resolve();
    for (auto& rp : resolved.all_patches_sorted()) {
        for (auto& fp : rp.fields) {
            all_patches.add_patch(rp.target_formid, fp.field, fp.op, fp.value, fp.source_mod, fp.priority);
        }
    }
}

auto final_resolved = all_patches.resolve();
```

With:

```cpp
mora::FactDB db(pool);
mora::Evaluator evaluator(pool, diags, db);
for (auto& mod : modules) {
    evaluator.evaluate_module(mod, db);
}
```

Note the const removal — `db` is written to.

- [ ] **Step 2: Replace the conflict branch + dump**

Replace (lines ~715..739):

```cpp
if (show_conflicts) {
    const auto& conflicts = final_resolved.get_conflicts();
    if (conflicts.empty()) {
        mora::log::info("  no conflicts\n");
    } else {
        mora::log::info("  {} conflict(s)\n", conflicts.size());
    }
    return 0;
}

mora::log::info("  mora inspect \xe2\x80\x94 {} patches (from {} files)\n\n",
    final_resolved.patch_count(), files.size());

auto all_sorted = final_resolved.all_patches_sorted();
if (all_sorted.empty()) {
    mora::log::info("  (no patches)\n");
} else {
    for (auto& rp : all_sorted) {
        mora::log::info("  0x{:08X}:\n", rp.target_formid);
        for (auto& fp : rp.fields) {
            mora::log::info("    {}: {} {}\n", field_name(fp.field), op_prefix(fp.op), format_value(fp.value, pool));
        }
        mora::log::info("\n");
    }
}
```

With:

```cpp
(void)show_conflicts;  // --show-conflicts is no longer meaningful

struct EffectRel { const char* rel_name; const char* op_str; };
constexpr EffectRel kEffectRels[] = {
    {"skyrim/set",      "="},
    {"skyrim/add",      "+="},
    {"skyrim/remove",   "-="},
    {"skyrim/multiply", "*="},
};

size_t total = 0;
for (const auto& er : kEffectRels) {
    total += db.fact_count(pool.intern(er.rel_name));
}

mora::log::info("  mora inspect \xe2\x80\x94 {} effect fact(s) (from {} files)\n\n",
                 total, files.size());

if (total == 0) {
    mora::log::info("  (no effect facts)\n");
    return 0;
}

for (const auto& er : kEffectRels) {
    auto rel_id = pool.intern(er.rel_name);
    const auto& tuples = db.get_relation(rel_id);
    if (tuples.empty()) continue;
    for (const auto& t : tuples) {
        if (t.size() < 3) continue;
        auto const formid = t[0].kind() == mora::Value::Kind::FormID
                              ? t[0].as_formid() : 0u;
        auto const field_kw = t[1].kind() == mora::Value::Kind::Keyword
                                ? pool.get(t[1].as_keyword())
                                : std::string_view{"?"};
        mora::log::info("  0x{:08X}: {} {} {}\n",
                         formid, field_kw, er.op_str,
                         format_value(t[2], pool));
    }
}
```

- [ ] **Step 3: Leave `--show-conflicts` flag parsing as-is for now**

Do NOT rip out the CLI flag — the arg-parsing indirection would spread this plan further. The flag just becomes a no-op. If `cmd_inspect` signature has `bool show_conflicts`, the `(void)show_conflicts;` line suppresses the unused-parameter warning.

- [ ] **Step 4: Build and confirm**

```
xmake build mora 2>&1 | head -40
```

Expected: succeeds. `grep 'PatchSet\|format_value\|op_prefix' src/main.cpp` — only `format_value` references remain (it's still defined and used).

---

### Task 4: Delete files — PatchSet, PatchBuffer, effect_facts, pipeline_evaluator, src/emit/

**Files (all deleted):**
- `include/mora/eval/patch_set.h`
- `src/eval/patch_set.cpp`
- `include/mora/eval/patch_buffer.h`
- `src/eval/patch_buffer.cpp`
- `include/mora/eval/effect_facts.h`
- `src/eval/effect_facts.cpp`
- `include/mora/eval/pipeline_evaluator.h`
- `src/eval/pipeline_evaluator.cpp`
- `include/mora/emit/arrangement.h`
- `include/mora/emit/arrangement_emit.h`
- `include/mora/emit/flat_file_writer.h`
- `include/mora/emit/patch_file_v2.h`
- `include/mora/emit/patch_table.h`
- `src/emit/arrangement_emit.cpp`
- `src/emit/flat_file_writer.cpp`
- `src/emit/patch_table.cpp`
- `include/mora/eval/phase_classifier.h` — **keep**, PhaseClassifier is still used by cmd_compile (classification phase).

**File modified:**
- `xmake.lua`: remove `"src/emit/*.cpp",` from the `mora_lib` glob (line ~176).

- [ ] **Step 1: Delete the files**

```bash
rm include/mora/eval/patch_set.h src/eval/patch_set.cpp
rm include/mora/eval/patch_buffer.h src/eval/patch_buffer.cpp
rm include/mora/eval/effect_facts.h src/eval/effect_facts.cpp
rm include/mora/eval/pipeline_evaluator.h src/eval/pipeline_evaluator.cpp
rm -r include/mora/emit src/emit
```

- [ ] **Step 2: Update `xmake.lua`**

Edit `xmake.lua` line ~176. Change:

```lua
add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
          "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
          "src/cli/*.cpp", "src/eval/*.cpp", "src/ext/*.cpp",
          "src/emit/*.cpp",
          ...
```

to:

```lua
add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
          "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
          "src/cli/*.cpp", "src/eval/*.cpp", "src/ext/*.cpp",
          ...
```

(Delete the single `"src/emit/*.cpp",` line.)

- [ ] **Step 3: Build and see which tests break**

```
xmake build mora_lib 2>&1 | tail -10
xmake build mora 2>&1 | tail -10
```

Both expected to pass. Then:

```
xmake build 2>&1 | tail -40
```

Expected failures: test files that still `#include` deleted headers. Task 5 handles those.

---

### Task 5: Delete obsolete tests + update `test_cli_parquet_sink.cpp`

**Files deleted:**
- `tests/patch_set_test.cpp`
- `tests/patch_buffer_test.cpp`
- `tests/patch_table_test.cpp`
- `tests/pipeline_evaluator_test.cpp`
- `tests/cli/test_cli_writes_v2.cpp`
- `tests/cli/test_effect_facts_bridge.cpp`
- `tests/emit/test_arrangements_in_patch_file.cpp`
- `tests/emit/test_patch_file_v2_roundtrip.cpp`
- `tests/emit/test_patch_file_v2_types.cpp`
- `tests/emit/test_string_patch_fast_path.cpp`
- `tests/emit/` (entire directory, after the files above are removed)

**Files modified:**
- `tests/cli/test_cli_parquet_sink.cpp` — Plan 6 M2 case (`EffectFactsBridgeRoundTripThroughTaggedColumns`) currently builds a `PatchSet` → `ResolvedPatchSet` → bridges to FactDB via `populate_effect_facts`. Switch it to populate `FactDB` directly (that's what the test was proving anyway — tagged-column encoding in the parquet sink).

- [ ] **Step 1: Delete the obsolete test files**

```bash
rm tests/patch_set_test.cpp tests/patch_buffer_test.cpp
rm tests/patch_table_test.cpp tests/pipeline_evaluator_test.cpp
rm tests/cli/test_cli_writes_v2.cpp
rm tests/cli/test_effect_facts_bridge.cpp
rm -r tests/emit
```

- [ ] **Step 2: Rewrite the Plan 6 M2 case in `test_cli_parquet_sink.cpp`**

Find the `EffectFactsBridgeRoundTripThroughTaggedColumns` case. Replace its PatchSet-building block with direct FactDB population. Example shape (exact content: open the current file, locate the block that calls `ps.add_patch(...)` / `populate_effect_facts(...)` / whatever Plan 7 left, and replace with):

```cpp
mora::StringPool pool;
mora::FactDB db(pool);

auto rel_set = pool.intern("skyrim/set");
db.configure_relation(rel_set, /*arity*/ 3, /*indexed*/ {0});

uint32_t const form = 0x000A0B0C;

auto add_set = [&](mora::FieldId field, mora::Value v) {
    auto kw = mora::Value::make_keyword(
        pool.intern(mora::field_id_name(field)));
    db.add_fact(rel_set, mora::Tuple{
        mora::Value::make_formid(form), kw, v});
};

add_set(mora::FieldId::Name,      mora::Value::make_string(pool.intern("Skeever")));
add_set(mora::FieldId::GoldValue, mora::Value::make_int(100));
add_set(mora::FieldId::Damage,    mora::Value::make_float(12.5));
add_set(mora::FieldId::Race,      mora::Value::make_formid(0x00013746));
```

Keep the assertions on the emitted parquet file identical — they're what the test is actually proving.

Drop any `#include "mora/eval/patch_set.h"` / `"mora/eval/effect_facts.h"` / `"mora/eval/patch_buffer.h"` at the top of the file. Keep `"mora/eval/fact_db.h"` and `"mora/model/field_names.h"`.

- [ ] **Step 3: Build and run parquet tests**

```
xmake build parquet_tests 2>&1 | tail -20
xmake run parquet_tests 2>&1 | tail -40
```

Expected: all parquet tests green, including the updated tagged-column case.

---

### Task 6: Fix up incidental test references

**Files potentially affected** (from the pre-plan grep for PatchSet/PatchBuffer/PatchEntry/patch_table/populate_effect_facts/evaluate_static):
- `tests/backend_integration_test.cpp`
- `tests/evaluator_test.cpp`
- `tests/operators_test.cpp`
- `tests/eval/test_builtin_fns.cpp`

Any test that called `evaluator.evaluate_static(mod)` needs updating to `evaluator.evaluate_module(mod, db)`. Any test that asserted on PatchSet contents needs to switch to FactDB assertions (or be deleted if its subject is gone).

- [ ] **Step 1: Build tests target and read the errors**

```
xmake build 2>&1 | tail -60
```

Expected: some test files fail to compile. Read each error carefully — they tell you exactly what to fix.

- [ ] **Step 2: For each failing test file, apply one of three fixes**

1. **Calls `evaluate_static`:** Change to `evaluate_module(mod, db)` and assert on `db.get_relation(pool.intern("skyrim/set"))` (or sibling) instead of on the returned PatchSet.
2. **Only uses PatchSet for a trivial check the FactDB already covers:** switch to FactDB check.
3. **Entire test's subject is deleted** (e.g. tests the source_mod field of RawPatch): delete the `TEST(...)` case.

For each edit, re-run:

```
xmake build <target_of_failing_test> 2>&1 | tail -20
```

Expected: that target now builds. Repeat for each failing test target.

- [ ] **Step 3: Run the full test suite**

```
xmake build 2>&1 | tail -10
xmake run 2>&1 | tail -30
```

Expected: all test binaries pass. If a previously-passing test now fails because its semantics actually depended on PatchSet's resolution logic (priority winner, conflict detection), either:
- Adjust the test to the new semantics (multiple tuples present instead of one winner), OR
- Delete the test if it was solely probing removed behavior.

Document the call out in the commit message body if any test was deleted in this step.

---

### Task 7: Add end-to-end evaluator → FactDB test

**Files:**
- Create: `tests/eval/test_evaluator_effect_facts.cpp`

This replaces the coverage the deleted `test_effect_facts_bridge.cpp` provided. It runs a tiny `.mora` program through the evaluator from parse → effect facts in the FactDB, without any PatchSet/bridge in between.

- [ ] **Step 1: Write the test**

```cpp
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"

#include <gtest/gtest.h>

namespace {

mora::Module parse_and_resolve(mora::StringPool& pool,
                                mora::DiagBag& diags,
                                const std::string& source) {
    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    return mod;
}

TEST(EvaluatorEffectFacts, SetProducesSkyrimSetTuple) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Minimal program: seed a fact, fire a single set_gold_value rule.
    std::string const source = R"(
        module test;
        fact gold_target(0x000DEFEA);
        rule give_gold {
            gold_target(?npc) => set_gold_value(?npc, 100);
        }
    )";

    auto mod = parse_and_resolve(pool, diags, source);
    ASSERT_FALSE(diags.has_errors()) << "parse/resolve errors";

    mora::FactDB db(pool);
    // Seed the input fact into db before evaluation.
    auto gold_target = pool.intern("gold_target");
    db.configure_relation(gold_target, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(gold_target, mora::Tuple{
        mora::Value::make_formid(0x000DEFEA)});

    mora::Evaluator evaluator(pool, diags, db);
    evaluator.evaluate_module(mod, db);

    auto rel_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(rel_set);
    ASSERT_EQ(tuples.size(), 1U);

    const auto& t = tuples.front();
    ASSERT_EQ(t.size(), 3U);
    EXPECT_EQ(t[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(t[0].as_formid(), 0x000DEFEAu);
    EXPECT_EQ(t[1].kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
    EXPECT_EQ(t[2].kind(), mora::Value::Kind::Int);
    EXPECT_EQ(t[2].as_int(), 100);
}

} // namespace
```

**Note on the source syntax:** if the .mora grammar details above don't match current syntax verbatim, consult any existing evaluator test for the exact idiom — parser/lexer tests in `tests/evaluator_test.cpp` show the in-use surface. The important contract: input fact in, one set-verb rule fires, one `skyrim/set` tuple comes out.

**Note on symbol→formid resolution:** if the test's `set_gold_value(?npc, 100)` resolution needs an editor-ID map (i.e. if `?npc` must go through symbol table), seed via `evaluator.set_symbol_formid(...)` the same way `cmd_compile` does. Keep the test self-contained — no ESP, no data-dir, just the evaluator.

- [ ] **Step 2: Build and run the new test**

```
xmake build eval_tests 2>&1 | tail -20
xmake run eval_tests -- --gtest_filter=EvaluatorEffectFacts.* 2>&1 | tail -20
```

Expected: the new test builds and passes. If the `.mora` source above won't parse (surface mismatch) or the rule doesn't fire (binding/resolution issue), adjust the source string and the seeded symbol map until it passes. The test must actually exercise the evaluator path, not a mock.

- [ ] **Step 3: Full test run + CLI smoke**

```
xmake build 2>&1 | tail -5
xmake run 2>&1 | tail -10
```

Then:

```
xmake run mora -- compile test_data/minimal --output-dir /tmp/mora-p8-smoke --sink parquet.snapshot=/tmp/mora-p8-smoke/out 2>&1 | tail -20
echo "exit: $?"
ls -la /tmp/mora-p8-smoke/
```

Expected: exit 0. `/tmp/mora-p8-smoke/` contains parquet files (one per output relation with at least one row) and **no** `mora_patches.bin`.

---

### Task 8: Commit

- [ ] **Step 1: Final build + tests green**

```
xmake build 2>&1 | tail -5
xmake run 2>&1 | tail -15
```

Expected: everything builds, all tests pass.

- [ ] **Step 2: Sanity-check the diff scope**

```
git status
git diff --stat eaeff87..HEAD
```

Expected deletions: ~20 files (`src/emit/*`, `include/mora/emit/*`, `src/eval/{patch_set,patch_buffer,effect_facts,pipeline_evaluator}.cpp` + their headers, 4 test files, `tests/emit/` contents).

Expected modifications: `src/main.cpp`, `include/mora/eval/evaluator.h`, `src/eval/evaluator.cpp`, `xmake.lua`, `tests/cli/test_cli_parquet_sink.cpp`, plus any tests fixed in Task 6.

Expected additions: `tests/eval/test_evaluator_effect_facts.cpp`.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: evaluator writes effect facts directly; delete PatchSet + src/emit/

Evaluator::evaluate_module now fills a FactDB& with tuples in
skyrim/{set,add,remove,multiply}, replacing the PatchSet → ResolvedPatchSet →
populate_effect_facts bridge. cmd_compile drops PatchBuffer, mora_patches.bin,
write_patch_file, and the arrangements/string-table pipeline entirely; parquet
sinks are the only snapshot output.

Deleted subsystems:
- src/eval/patch_set.{h,cpp}
- src/eval/patch_buffer.{h,cpp}
- src/eval/effect_facts.{h,cpp} (bridge collapsed into evaluator)
- src/eval/pipeline_evaluator.{h,cpp} (unused in v3)
- src/emit/ entirely
- include/mora/emit/ entirely
- 4 obsolete test files (patch_set_test, patch_buffer_test, patch_table_test,
  pipeline_evaluator_test) + tests/emit/ + test_cli_writes_v2 +
  test_effect_facts_bridge

Behavior change: priority-based last-write-wins and the conflict list are
gone. They were artifacts of the runtime-DLL patch-application model deleted
in Plan 2. Rules that previously relied on priority to override will now emit
both tuples into skyrim/set; downstream consumers resolve.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Expected: commit succeeds. `git status` clean.

---

## Verification

Before marking Plan 8 done, confirm:

1. `grep -rn 'PatchSet\|PatchBuffer\|mora_patches\|populate_effect_facts\|write_patch_file' src include` is empty.
2. `ls src/emit include/mora/emit 2>&1` reports not-found for both.
3. `xmake run` — every test binary passes.
4. CLI smoke on `test_data/minimal` produces parquet files and no `mora_patches.bin`.
5. Branch is `mora-v3-foundation`, 18 commits above master (17 previous + 1 from this plan).

## Behavior changes called out (inform user in commit + summary)

- **Priority** (`@priority N` on rules, `source_mod` tracking) no longer affects output. Every effect rule emits one tuple per match into the corresponding `skyrim/*` relation. Duplicate tuples collapse via FactDB's natural set semantics; genuine multi-rule conflicts produce multiple distinct tuples.
- **Conflict list** (previously surfaced by `mora inspect --show-conflicts`) is gone. The flag parses but is a no-op.
- **`mora_patches.bin`** is no longer written. Consumers of the binary format (the SKSE DLL, deleted in Plan 2) have no in-tree code path in v3. Parquet is the sole snapshot format.

## Explicitly NOT in scope

- Vectorized evaluator rewrite (Plan 9+).
- Columnar chunked FactDB storage (Plan 9+).
- Dropping verb keywords from the grammar (Plan 10).
- Type registry / nominal tags on columns (Plan 11+).

## Critical files

- `/home/tbaldrid/oss/mora/include/mora/eval/evaluator.h`
- `/home/tbaldrid/oss/mora/src/eval/evaluator.cpp`
- `/home/tbaldrid/oss/mora/src/main.cpp`
- `/home/tbaldrid/oss/mora/xmake.lua`
- `/home/tbaldrid/oss/mora/tests/cli/test_cli_parquet_sink.cpp`
- `/home/tbaldrid/oss/mora/tests/eval/test_evaluator_effect_facts.cpp` (new)
