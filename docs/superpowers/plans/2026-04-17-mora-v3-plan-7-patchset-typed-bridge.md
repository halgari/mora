# Mora v3 — Plan 7: Route Effect-Facts Bridge Through Typed `ResolvedPatchSet`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change Plan 5's `populate_effect_facts` bridge to consume the typed `ResolvedPatchSet` (with `Value`-valued `FieldPatch`es) instead of the byte-packed `PatchBuffer`. Eliminates the `PatchValueType` decode switch and the Plan 5 `StringIndex`-is-skipped workaround — string-valued effect facts now land in `skyrim/set` with proper `Value::Kind::String` and round-trip through Plan 6's tagged-column parquet encoding.

**Architecture:** `evaluate_mora_rules` already builds a `mora::ResolvedPatchSet` (`mora_resolved`, around `src/main.cpp:365`) before flattening it into the binary-patch-file `PatchBuffer`. We expose that typed artifact via an out-parameter; `cmd_compile` then passes it to the bridge. The bridge iterates `all_patches_sorted()` and uses `fp.value` directly — no more bit-casts, no StringIndex byte-offset gotcha. The downstream `write_patch_file` path is untouched — `mora_patches.bin` continues to be emitted from the same `PatchBuffer`, so this plan is strictly additive on the output surface.

**Tech Stack:** C++20, xmake, gtest.

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md`. Plan 7 is the last hop before the spec's "evaluator stops producing `PatchSet`" transition (Plan 8+). Once Plan 8 teaches the evaluator to derive directly into the `skyrim/set`/`add`/`remove`/`multiply` FactDB relations, `src/emit/` and the `PatchSet`/`PatchBuffer` path can retire.

**Branch:** continue on `mora-v3-foundation`. Plan 7 layers one commit on top of Plan 6's sixteen.

**Baseline:** HEAD `a58118d` (P6 M2). Clean tree. `xmake build` green. 91 test binaries pass.

---

## Design notes (read before editing)

1. **Minimal-churn signature change.** `evaluate_mora_rules` stays `void`; gains a new out-parameter `mora::ResolvedPatchSet& out_resolved` right next to the existing `PatchBuffer& patch_buf`. The caller in `cmd_compile` declares a local `mora::ResolvedPatchSet mora_resolved;` before the call and passes it by reference. Inside `evaluate_mora_rules`, the existing `auto mora_resolved = all_patches.resolve();` line becomes `out_resolved = all_patches.resolve();`.

2. **Bridge signature change.** `populate_effect_facts(const PatchBuffer&, FactDB&, StringPool&)` becomes `populate_effect_facts(const ResolvedPatchSet&, FactDB&, StringPool&)`. The body rewrites cleanly: iterate `patches.all_patches_sorted()`; for each `ResolvedPatch` + `FieldPatch`, the field, op, and typed `Value` are already there. No `PatchValueType` decode; no `bit_cast`; no skip for `StringIndex`. A single pre-interning of the four relation names at function entry still applies.

3. **`#include "mora/emit/patch_table.h"` no longer needed** in `effect_facts.cpp` once the `PatchValueType` references are gone. `mora/eval/patch_buffer.h` also no longer needed. Replace with `mora/eval/patch_set.h` (for `ResolvedPatchSet`, `FieldPatch`, `FieldOp`).

4. **Tests: mechanical porting + adding back the string case.** The Plan 5 tests in `tests/cli/test_effect_facts_bridge.cpp` use `PatchBuffer::emit` with `PatchValueType` tags. Port each test to build a `PatchSet` with typed `Value`s via `add_patch(formid, field, op, value, source_mod, priority)`, then call `resolve()` and hand the resulting `ResolvedPatchSet` to the bridge. The `StringIndexEntriesAreSkipped` test is retired (no longer a relevant behavior). `FloatAndFormIDValuesRoundTrip` grows back into a proper `StringFloatAndFormIDValuesRoundTrip` with a String case re-added.

5. **The Plan 6 M2 integration test** in `tests/cli/test_cli_parquet_sink.cpp` (`EffectFactsBridgeRoundTripThroughTaggedColumns`) likewise ports from `PatchBuffer` to `ResolvedPatchSet` and gains a String entry. The assertion body is extended to verify the `col2_string` column receives the string payload with kind tag `"String"`.

6. **Source mod + priority on `add_patch`.** Tests don't care about these — pass a dummy `StringId{}` for `source_mod` and `0` for `priority`. Resolution still picks the single patch per (formid, field, op) because there's only one.

7. **No changes to `src/emit/`.** `write_patch_file` continues to call `serialize_patch_table(patch_buf.entries(), ...)`. Only the effect-facts bridge's input source changes.

---

## File Map

**Modified:**
- `include/mora/eval/effect_facts.h` — declaration changes: `const PatchBuffer&` → `const ResolvedPatchSet&`; forward-decl updates.
- `src/eval/effect_facts.cpp` — body rewritten for typed iteration; deletes the `PatchValueType` switch + the `StringIndex` skip.
- `src/main.cpp` — `evaluate_mora_rules` gains an out-parameter; `cmd_compile` declares + passes the `mora_resolved` local.
- `tests/cli/test_effect_facts_bridge.cpp` — 4 cases ported from PatchBuffer → ResolvedPatchSet; StringIndex-skip case retired; String case added.
- `tests/cli/test_cli_parquet_sink.cpp` — Plan 6 M2 case ported + String assertion added.

**No new files.**

---

## Baseline

- [ ] **Step B1: Verify branch state**

```bash
cd /home/tbaldrid/oss/mora
git status
git log master..HEAD --oneline | head -3
```

Expected: clean tree; 16 commits; HEAD `a58118d`.

- [ ] **Step B2: Verify build + tests**

```bash
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean; 91 binaries pass.

---

## Task 1: Change the bridge signature + include

**Files:**
- Modify: `include/mora/eval/effect_facts.h`

- [ ] **Step 1.1: Edit the header**

Current file body:

```cpp
#pragma once

#include "mora/core/string_pool.h"

namespace mora {

class FactDB;
class PatchBuffer;

// Walks a PatchBuffer's entries and populates the four Skyrim effect
// relations (skyrim/set, skyrim/add, skyrim/remove, skyrim/multiply)
// with `(FormID, :FieldName, Value)` tuples. Configures the relations
// lazily on first use.
void populate_effect_facts(const PatchBuffer& buf,
                            FactDB& db,
                            StringPool& pool);

} // namespace mora
```

Replace with:

```cpp
#pragma once

#include "mora/core/string_pool.h"

namespace mora {

class FactDB;
class ResolvedPatchSet;

// Walks a ResolvedPatchSet and populates the four Skyrim effect
// relations (skyrim/set, skyrim/add, skyrim/remove, skyrim/multiply)
// with `(FormID, :FieldName, Value)` tuples.
//
// Values are carried through as their typed Value form (FormID, Int,
// Float, String, Bool) because ResolvedPatchSet preserves StringPool
// references on String values — unlike PatchBuffer's byte-packed
// uint64 encoding, which requires the binary-patch StringTable to
// decode and motivated a workaround skip in earlier plans.
//
// Relations are configured lazily on first emit per relation (arity 3,
// column 0 indexed on the FormID).
void populate_effect_facts(const ResolvedPatchSet& patches,
                            FactDB& db,
                            StringPool& pool);

} // namespace mora
```

- [ ] **Step 1.2: Build expected to fail**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -5
```

Expected: `src/eval/effect_facts.cpp` and the call sites in `main.cpp` + tests now reference a signature that doesn't match their callers. Tasks 2–5 fix each caller.

## Task 2: Rewrite the bridge implementation

**Files:**
- Modify: `src/eval/effect_facts.cpp`

- [ ] **Step 2.1: Overwrite the file**

Replace the entire contents of `src/eval/effect_facts.cpp` with:

```cpp
#include "mora/eval/effect_facts.h"

#include "mora/data/value.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/model/field_names.h"

#include <unordered_set>

namespace mora {

void populate_effect_facts(const ResolvedPatchSet& patches,
                            FactDB& db,
                            StringPool& pool) {
    auto id_set      = pool.intern("skyrim/set");
    auto id_add      = pool.intern("skyrim/add");
    auto id_remove   = pool.intern("skyrim/remove");
    auto id_multiply = pool.intern("skyrim/multiply");

    std::unordered_set<uint32_t> configured;
    auto ensure_configured = [&](StringId rel_id) {
        if (configured.insert(rel_id.index).second) {
            db.configure_relation(rel_id, /*arity*/ 3, /*indexed*/ {0});
        }
    };

    for (const auto& rp : patches.all_patches_sorted()) {
        for (const auto& fp : rp.fields) {
            StringId rel_id;
            switch (fp.op) {
                case FieldOp::Set:      rel_id = id_set;      break;
                case FieldOp::Add:      rel_id = id_add;      break;
                case FieldOp::Remove:   rel_id = id_remove;   break;
                case FieldOp::Multiply: rel_id = id_multiply; break;
            }
            ensure_configured(rel_id);

            auto field_kw = Value::make_keyword(
                pool.intern(field_id_name(fp.field)));

            db.add_fact(rel_id, Tuple{
                Value::make_formid(rp.target_formid),
                field_kw,
                fp.value,   // already typed — no decode needed
            });
        }
    }
}

} // namespace mora
```

Note the changes from the Plan 5 version:
- `#include "mora/emit/patch_table.h"` removed (no PatchValueType).
- `#include "mora/eval/patch_buffer.h"` removed.
- `#include <bit>` removed (no `std::bit_cast`).
- The inner `PatchValueType` switch is gone — `fp.value` is already a typed `Value`.
- The `StringIndex`-is-skipped comment block is gone — strings now flow through cleanly.

- [ ] **Step 2.2: Build — `src/eval/effect_facts.cpp` should compile**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: the library builds. `main.cpp` still won't link yet because its call site in `cmd_compile` passes a `PatchBuffer` to the bridge.

## Task 3: Expose `mora_resolved` from `evaluate_mora_rules`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 3.1: Add the out-parameter to `evaluate_mora_rules`**

Find the existing function signature at `src/main.cpp:339`:

```cpp
static void evaluate_mora_rules(
    CheckResult& cr, mora::Evaluator& evaluator,
    mora::PatchBuffer& patch_buf, std::vector<uint8_t>& string_table_out,
    mora::Output& out)
```

Change to:

```cpp
static void evaluate_mora_rules(
    CheckResult& cr, mora::Evaluator& evaluator,
    mora::PatchBuffer& patch_buf, std::vector<uint8_t>& string_table_out,
    mora::ResolvedPatchSet& out_resolved,
    mora::Output& out)
```

- [ ] **Step 3.2: Populate the out-parameter inside the function**

The function body currently has a line (around `src/main.cpp:365`):

```cpp
    auto mora_resolved = all_patches.resolve();
    std::vector<mora::PatchEntry> entries;
    string_table_out = mora::build_patch_entries_and_string_table(
        mora_resolved, cr.pool, entries);
```

Change to assign the out-parameter instead of a local:

```cpp
    out_resolved = all_patches.resolve();
    std::vector<mora::PatchEntry> entries;
    string_table_out = mora::build_patch_entries_and_string_table(
        out_resolved, cr.pool, entries);
```

- [ ] **Step 3.3: Update the caller in `cmd_compile`**

Find the call at `src/main.cpp:594`:

```cpp
    evaluate_mora_rules(cr, evaluator, patch_buf, string_table, out);
```

And the `populate_effect_facts` call on the line right after (added in Plan 5 M2):

```cpp
    mora::populate_effect_facts(patch_buf, db, cr.pool);
```

Declare `mora_resolved` before the `evaluate_mora_rules` call, pass it through, then pass it to the bridge:

```cpp
    mora::ResolvedPatchSet mora_resolved;
    evaluate_mora_rules(cr, evaluator, patch_buf, string_table,
                        mora_resolved, out);
    mora::populate_effect_facts(mora_resolved, db, cr.pool);
```

- [ ] **Step 3.4: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -5
```

Expected: `mora` binary builds clean. Existing tests under `mora_lib` and the extensions all still use the old `PatchBuffer`-taking bridge signature, so the test binaries now fail to link. Tasks 4–5 update them.

## Task 4: Port `test_effect_facts_bridge.cpp`

**Files:**
- Modify: `tests/cli/test_effect_facts_bridge.cpp`

The 4 cases currently build a `PatchBuffer` via `buf.emit(...)`. Port each to build a `PatchSet` with typed `Value`s and call `resolve()` for the bridge input. Retire the `StringIndexEntriesAreSkipped` case (no longer applicable). Rename `FloatAndFormIDValuesRoundTrip` → `StringFloatAndFormIDValuesRoundTrip` and restore the String case dropped in Plan 5's amend.

- [ ] **Step 4.1: Overwrite the file**

Replace the entire contents with:

```cpp
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/effect_facts.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/model/relations.h"

#include <gtest/gtest.h>

namespace {

TEST(EffectFactsBridge, OneEntryPerOp) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchSet ps;

    auto const form_id = uint32_t{0x000DEFEA};
    auto const field   = mora::FieldId::GoldValue;

    ps.add_patch(form_id, field, mora::FieldOp::Set,
                 mora::Value::make_int(100),
                 mora::StringId{}, /*priority*/ 0);
    ps.add_patch(form_id, field, mora::FieldOp::Add,
                 mora::Value::make_int(50),
                 mora::StringId{}, /*priority*/ 0);
    ps.add_patch(form_id, field, mora::FieldOp::Remove,
                 mora::Value::make_int(25),
                 mora::StringId{}, /*priority*/ 0);
    ps.add_patch(form_id, field, mora::FieldOp::Multiply,
                 mora::Value::make_int(2),
                 mora::StringId{}, /*priority*/ 0);

    mora::populate_effect_facts(ps.resolve(), db, pool);

    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        ASSERT_EQ(db.fact_count(rel_id), 1U)
            << "relation " << rel_name << " has unexpected count";
    }

    auto id_set = pool.intern("skyrim/set");
    const auto& set_tuples = db.get_relation(id_set);
    ASSERT_EQ(set_tuples.size(), 1U);
    const auto& t = set_tuples.front();
    ASSERT_EQ(t.size(), 3U);
    EXPECT_EQ(t[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(t[0].as_formid(), form_id);
    EXPECT_EQ(t[1].kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
    EXPECT_EQ(t[2].kind(), mora::Value::Kind::Int);
    EXPECT_EQ(t[2].as_int(), 100);
}

TEST(EffectFactsBridge, EmptyPatchSetPopulatesNothing) {
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchSet ps;

    mora::populate_effect_facts(ps.resolve(), db, pool);

    for (const char* rel_name : {"skyrim/set", "skyrim/add",
                                  "skyrim/remove", "skyrim/multiply"}) {
        auto rel_id = pool.intern(rel_name);
        EXPECT_EQ(db.fact_count(rel_id), 0U)
            << "relation " << rel_name << " should be empty";
    }
}

TEST(EffectFactsBridge, StringFloatAndFormIDValuesRoundTrip) {
    // Covers the three non-Int decode paths — all now work without
    // any special-case handling, because fp.value carries the typed
    // Value directly (Plan 7's key benefit over the Plan 5 byte-packed
    // PatchBuffer approach).
    mora::StringPool pool;
    mora::FactDB db(pool);
    mora::PatchSet ps;

    auto form = uint32_t{0x000A0B0C};

    auto const interned_name = pool.intern("Skeever");
    ps.add_patch(form, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(interned_name),
                 mora::StringId{}, /*priority*/ 0);

    double const damage = 12.5;
    ps.add_patch(form, mora::FieldId::Damage, mora::FieldOp::Set,
                 mora::Value::make_float(damage),
                 mora::StringId{}, /*priority*/ 0);

    uint32_t const race_form = 0x00013746;
    ps.add_patch(form, mora::FieldId::Race, mora::FieldOp::Set,
                 mora::Value::make_formid(race_form),
                 mora::StringId{}, /*priority*/ 0);

    mora::populate_effect_facts(ps.resolve(), db, pool);

    auto id_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(id_set);
    ASSERT_EQ(tuples.size(), 3U);

    const mora::Tuple* name_tuple   = nullptr;
    const mora::Tuple* damage_tuple = nullptr;
    const mora::Tuple* race_tuple   = nullptr;
    for (const auto& t : tuples) {
        auto kw = pool.get(t[1].as_keyword());
        if      (kw == "Name")   name_tuple   = &t;
        else if (kw == "Damage") damage_tuple = &t;
        else if (kw == "Race")   race_tuple   = &t;
    }
    ASSERT_NE(name_tuple,   nullptr);
    ASSERT_NE(damage_tuple, nullptr);
    ASSERT_NE(race_tuple,   nullptr);

    EXPECT_EQ((*name_tuple)[2].kind(), mora::Value::Kind::String);
    EXPECT_EQ(pool.get((*name_tuple)[2].as_string()), "Skeever");

    EXPECT_EQ((*damage_tuple)[2].kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ((*damage_tuple)[2].as_float(), damage);

    EXPECT_EQ((*race_tuple)[2].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ((*race_tuple)[2].as_formid(), race_form);
}

} // namespace
```

- [ ] **Step 4.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake f -p linux -m debug --yes 2>&1 | tail -2
xmake build test_effect_facts_bridge 2>&1 | tail -5
./build/linux/x86_64/debug/test_effect_facts_bridge 2>&1 | tail -10
```

Expected: 3 cases PASS (reduced from 4 — `StringIndexEntriesAreSkipped` retired).

## Task 5: Port the Plan 6 M2 CLI round-trip test

**Files:**
- Modify: `tests/cli/test_cli_parquet_sink.cpp`

The Plan 6 M2 case (`EffectFactsBridgeRoundTripThroughTaggedColumns`) currently builds a `PatchBuffer` and handles the Arrow UInt32→Int64 read-back dance. Port to a `PatchSet`; add a String entry; extend the readback assertions.

- [ ] **Step 5.1: Locate and edit the test**

In `tests/cli/test_cli_parquet_sink.cpp`, find `TEST(CliParquetSink, EffectFactsBridgeRoundTripThroughTaggedColumns)` (introduced in Plan 6 M2).

Inside that test, replace the current `mora::PatchBuffer buf;` block + all the `buf.emit(...)` calls + the `mora::populate_effect_facts(buf, ...)` line with:

```cpp
    mora::PatchSet ps;
    auto const form = uint32_t{0x000ABCDE};

    ps.add_patch(form, mora::FieldId::GoldValue, mora::FieldOp::Set,
                 mora::Value::make_int(750),
                 mora::StringId{}, /*priority*/ 0);

    double const weight = 2.5;
    ps.add_patch(form, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(weight),
                 mora::StringId{}, /*priority*/ 0);

    ps.add_patch(form, mora::FieldId::Race, mora::FieldOp::Set,
                 mora::Value::make_formid(0x01337F),
                 mora::StringId{}, /*priority*/ 0);

    // String case — previously broken under the PatchBuffer path
    // because PatchValueType::StringIndex encoded a byte offset, not
    // a StringPool index. Plan 7's typed ResolvedPatchSet path carries
    // the StringId directly, so the string round-trips.
    ps.add_patch(form, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Skeever")),
                 mora::StringId{}, /*priority*/ 0);

    mora::populate_effect_facts(ps.resolve(), db, pool);
```

Now the test expects 4 rows (not 3). Find the `ASSERT_EQ(table->num_rows(), 3);` assertion and update to `4`. Extend the readback loop to handle the `"Name"` row:

Find the existing row-matching loop:

```cpp
    bool seen_int = false, seen_float = false, seen_formid = false;
    for (int64_t i = 0; i < table->num_rows(); ++i) {
        auto field = field_col->GetString(i);
        auto kind  = kind_col->GetString(i);

        if (field == "GoldValue") {
            ...
        } else if (field == "Weight") {
            ...
        } else if (field == "Race") {
            ...
        }
    }

    EXPECT_TRUE(seen_int);
    EXPECT_TRUE(seen_float);
    EXPECT_TRUE(seen_formid);
```

Extend with a `seen_string` flag + a new `Name` branch. Also grab the `string_col` reference earlier:

```cpp
    auto string_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
```

(Place this alongside the other `*_col` declarations near the top of the readback block.)

Then the loop becomes:

```cpp
    bool seen_int = false, seen_float = false, seen_formid = false;
    bool seen_string = false;
    for (int64_t i = 0; i < table->num_rows(); ++i) {
        auto field = field_col->GetString(i);
        auto kind  = kind_col->GetString(i);

        if (field == "GoldValue") {
            EXPECT_EQ(kind, "Int");
            EXPECT_FALSE(int_col->IsNull(i));
            EXPECT_EQ(int_col->Value(i), 750);
            seen_int = true;
        } else if (field == "Weight") {
            EXPECT_EQ(kind, "Float");
            EXPECT_FALSE(float_col->IsNull(i));
            EXPECT_DOUBLE_EQ(float_col->Value(i), 2.5);
            seen_float = true;
        } else if (field == "Race") {
            EXPECT_EQ(kind, "FormID");
            auto col3_type = table->column(3)->type()->id();
            if (col3_type == arrow::Type::UINT32) {
                auto formid_col = std::static_pointer_cast<arrow::UInt32Array>(
                    table->column(3)->chunk(0));
                EXPECT_FALSE(formid_col->IsNull(i));
                EXPECT_EQ(formid_col->Value(i), 0x01337Fu);
            } else {
                auto formid_col = std::static_pointer_cast<arrow::Int64Array>(
                    table->column(3)->chunk(0));
                EXPECT_FALSE(formid_col->IsNull(i));
                EXPECT_EQ(formid_col->Value(i), 0x01337F);
            }
            seen_formid = true;
        } else if (field == "Name") {
            EXPECT_EQ(kind, "String");
            EXPECT_FALSE(string_col->IsNull(i));
            EXPECT_EQ(string_col->GetString(i), "Skeever");
            seen_string = true;
        }
    }

    EXPECT_TRUE(seen_int);
    EXPECT_TRUE(seen_float);
    EXPECT_TRUE(seen_formid);
    EXPECT_TRUE(seen_string);
```

Also drop any `#include <bit>` that was added in Plan 6 M2 purely for `std::bit_cast` — no longer needed. Keep `#include "mora/eval/patch_set.h"` for `PatchSet` + `FieldOp`. Drop `#include "mora/eval/patch_buffer.h"` + `#include "mora/emit/patch_table.h"` if they were added and aren't referenced by other cases in the same file.

Check other cases in `test_cli_parquet_sink.cpp` — if any of the prior cases (`DispatchesConfiguredSink`, `NoSinkConfiguredProducesNoFiles`, `OutputOnlyFilterEmitsOnlyFlaggedRelations`) use `PatchBuffer`, they don't need porting (they don't drive through the bridge). Leave them alone.

- [ ] **Step 5.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_cli_parquet_sink 2>&1 | tail -5
./build/linux/x86_64/debug/test_cli_parquet_sink 2>&1 | tail -15
```

Expected: 4 cases PASS (all 4 previously-green cases still pass; the M2 case now also handles the String entry).

## Task 6: Final build + full suite

- [ ] **Step 6.1: Fresh clean build**

```bash
cd /home/tbaldrid/oss/mora
xmake clean -a
xmake f -p linux -m debug --yes
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -3
```

Expected: clean; `100% tests passed, 0 test(s) failed out of 90` (91 prior − 1 retired `StringIndexEntriesAreSkipped`; the `test_effect_facts_bridge` binary still exists with 3 cases instead of 4).

- [ ] **Step 6.2: Straggler grep**

```bash
grep -nE "PatchValueType|bit_cast|StringIndexEntriesAreSkipped|PatchBuffer" src/eval/effect_facts.cpp tests/cli/test_effect_facts_bridge.cpp 2>&1
```

Expected: `effect_facts.cpp` has zero matches. `test_effect_facts_bridge.cpp` has zero matches. Other files may still mention these identifiers (main.cpp still uses PatchBuffer for binary patch emission — that's intentional).

- [ ] **Step 6.3: CLI smoke test**

```bash
cd /tmp && rm -rf mora-p7-smoke && mkdir -p mora-p7-smoke && cd mora-p7-smoke
echo "namespace smoke" > empty.mora
/home/tbaldrid/oss/mora/build/linux/x86_64/release/mora compile empty.mora \
    --data-dir /tmp/mora-p7-smoke \
    --sink "parquet.snapshot=./parq?output-only" 2>&1 | tail -3
echo "exit=$?"
ls -R parq 2>&1 | head -10
```

Expected: exit 0. Four empty `skyrim/*.parquet` files, same as Plan 6's smoke. Production rules still don't exercise the bridge end-to-end without real ESP data.

If the release binary is stale, rebuild first: `cd /home/tbaldrid/oss/mora && xmake f -p linux -m release --yes && xmake build && xmake f -p linux -m debug --yes`.

## Task 7: Commit

- [ ] **Step 7.1: Stage + commit**

Stage everything including the untracked plan doc, then commit via HEREDOC:

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: route effect-facts bridge through typed ResolvedPatchSet

populate_effect_facts now consumes the upstream ResolvedPatchSet
(which carries typed mora::Value instances with StringPool
references intact) instead of the downstream PatchBuffer (byte-
packed uint64 + PatchValueType tag). Eliminates the entire decode
switch + the Plan 5 StringIndex-is-skipped workaround.

Changes:
  * include/mora/eval/effect_facts.h — signature takes
    const ResolvedPatchSet&.
  * src/eval/effect_facts.cpp — body iterates
    patches.all_patches_sorted() and uses fp.value directly; no
    PatchValueType decode; no bit_cast; no skip for String
    values. Drops includes for patch_buffer.h, patch_table.h, <bit>.
  * src/main.cpp — evaluate_mora_rules gains an out-parameter
    `mora::ResolvedPatchSet& out_resolved`; cmd_compile declares
    a local and passes it to populate_effect_facts instead of
    patch_buf. The binary mora_patches.bin emission path
    (serialize_patch_table from patch_buf.entries()) is
    unchanged — PatchBuffer is still populated upstream for it.

Tests:
  * tests/cli/test_effect_facts_bridge.cpp — 4 Plan 5 cases
    ported from PatchBuffer to PatchSet; StringIndexEntriesAreSkipped
    retired (no longer applicable); FloatAndFormIDValuesRoundTrip
    grows back into StringFloatAndFormIDValuesRoundTrip with a
    String case. 3 cases total.
  * tests/cli/test_cli_parquet_sink.cpp — the Plan 6 M2 case
    EffectFactsBridgeRoundTripThroughTaggedColumns is ported
    from PatchBuffer to PatchSet and gains a String entry
    (FieldId::Name / "Skeever"); readback asserts the col2_string
    column receives it with kind tag "String".

Full suite: 90 test binaries pass (91 prior - 1 retired case in
the same test binary does not change the binary count; the other
test binary sizes are unchanged).

After this commit:
  * String-valued effect facts round-trip through parquet
    correctly for the first time.
  * The effect-facts bridge has a simpler, typed implementation
    that makes it easy to retire (Plan 8+) once the evaluator
    produces effect facts directly.

Part 7 of the v3 rewrite.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7.2: Verify**

```bash
git log -1 --stat
git log --oneline master..HEAD | head -5
```

Expected: one new commit on top of `a58118d`. Diff shows ~5 files changed (effect_facts.h, effect_facts.cpp, main.cpp, test_effect_facts_bridge.cpp, test_cli_parquet_sink.cpp) + the new plan doc.

---

## Done

After Task 7, Plan 7 is complete. Branch state:
- 17 commits on `mora-v3-foundation` (3 P1 + 3 P2 + 3 P3 + 3 P4 + 2 P5 + 2 P6 + 1 P7).
- `xmake build` green.
- 91 test binaries pass (with 1 retired gtest case inside the test_effect_facts_bridge binary, so 90 total-cases-in-that-binary).
- String-valued effect facts now round-trip through parquet correctly.

**Deferred to later plans:**
- Teach the evaluator to produce effect facts directly, retiring the bridge (Plan 8+).
- Delete `src/emit/` once the evaluator skips PatchSet (Plan 8+).
- Fix `FieldId::RaceForm → "Race(form)"` keyword-name round-trip issue (cosmetic).
- Retire the legacy `:Symbol → FormID` evaluator fallback.

**What's next (Plan 8 — not in this plan):**
Most natural candidate: **teach the evaluator to produce effect facts directly into the FactDB** instead of through the PatchSet intermediate. Once that's done:
- The effect-facts bridge becomes redundant (evaluator writes directly to `skyrim/set` et al.).
- `PatchSet` / `PatchBuffer` / `serialize_patch_table` / `mora_patches.bin` become retire-able.
- `src/emit/` can be deleted wholesale.

That's a significantly larger plan than Plan 7 — likely 3+ milestones covering evaluator rewrite, bridge retirement, and emit deletion.
