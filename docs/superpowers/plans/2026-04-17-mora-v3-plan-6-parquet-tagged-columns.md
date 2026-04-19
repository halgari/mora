# Mora v3 — Plan 6: Parquet Tagged-Column Encoding

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Teach the parquet snapshot sink to emit heterogeneous value columns (where different rows carry different `Value::Kind`) as six tagged Arrow fields instead of skipping the relation with a warning. This unblocks the effect facts produced by Plan 5's `populate_effect_facts` bridge from actually reaching parquet — when `skyrim/set` has a value column holding mixed Int/Float/FormID/String values, the sink now emits one self-describing parquet file per relation with a `_kind` column plus five nullable per-type value columns.

**Architecture:** Per-column heterogeneity detection replaces the current per-relation check. Homogeneous columns keep their single typed-column emission; heterogeneous columns expand into six Arrow fields (`<col>_kind` utf8 + `<col>_formid` nullable uint32 + `<col>_int` nullable int64 + `<col>_float` nullable float64 + `<col>_string` nullable utf8 + `<col>_bool` nullable bool). Each row sets exactly one of the nullable value columns based on its kind tag. List/Var values still cause whole-relation skips (no tagged encoding for those).

**Tech Stack:** C++20, xmake, gtest, Apache Arrow (already dep'd via Plan 3's xmake-repo override).

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md`. Plan 6 is a refinement of spec step 5's parquet-sink design to accommodate the "polymorphic value column" effect-relation schema introduced in spec steps 7 + 11 (and realized concretely in Plan 4 + 5).

**Branch:** continue on `mora-v3-foundation`. Plan 6 layers two commits on top of Plan 5's fourteen.

**Baseline:** HEAD `7bdc8b0` (P5 M2). Clean tree. `xmake build` green. 90 test binaries pass.

---

## Design notes (read before editing)

1. **Per-column heterogeneity detection.** The existing heterogeneity check in `ParquetSnapshotSink::emit` (in `extensions/parquet/src/snapshot_sink.cpp`, around lines 305–320 at baseline) walks every tuple and, on the first column-kind mismatch, flags the entire relation as heterogeneous and skips it. Replace that with a per-column scan: for each column `c`, collect the distinct kinds across tuples. If the set has size 1, the column is homogeneous (existing fast path via `arrow_type_for` + `build_column`). If size > 1, the column is heterogeneous → emit the six tagged sub-columns.

2. **Arity mismatch is still a whole-relation skip.** If any tuple has a different arity than the first tuple, the shape is broken (not a column-kind mismatch). Keep the existing `parquet-skip-heterogeneous` warning, but narrow its scope to arity-only. Rename the diagnostic code to `parquet-skip-arity-mismatch` for precision.

3. **List/Var still skip the whole relation.** The tagged encoding only covers scalar kinds. If any tuple's value at column `c` is `Kind::List` or `Kind::Var` (a planner placeholder — should never appear in stored facts), skip the whole relation with the existing `parquet-skip-unsupported-kind` warning.

4. **Arrow field naming convention.** For each input column at position `c`:
   - Homogeneous → one field: `col{c}` with the column's Arrow type (e.g. `col2` uint32).
   - Heterogeneous → six fields, in this order: `col{c}_kind` (utf8), `col{c}_formid` (uint32), `col{c}_int` (int64), `col{c}_float` (float64), `col{c}_string` (utf8), `col{c}_bool` (bool). The five value columns are nullable; the kind column is not.

5. **Row-level invariant.** In a tagged-column group at position `c`: exactly one of the five value columns is non-null per row, and the kind column names which one. Consumers read `col{c}_kind` to dispatch, then extract from the matching typed column.

6. **Arrow builder null support.** `arrow::UInt32Builder`, `Int64Builder`, `DoubleBuilder`, `StringBuilder`, `BooleanBuilder` all support `AppendNull()` out of the box. For a row whose value lands in `_int`, call `b_formid.AppendNull()`, `b_int.Append(value)`, `b_float.AppendNull()`, etc. The kind-column builder is a `StringBuilder` with `Append("Int")`.

7. **Kind names in the `_kind` column.** Use stable string names matching `Value::Kind`:
   - `Value::Kind::FormID` → `"FormID"`
   - `Value::Kind::Int` → `"Int"`
   - `Value::Kind::Float` → `"Float"`
   - `Value::Kind::String` → `"String"`
   - `Value::Kind::Keyword` → `"Keyword"` (kept as a distinct kind even though serialized via the utf8 `_string` slot — Plan 5's `DistinctFromString` invariant has us keeping the kinds distinct even when they share storage width)
   - `Value::Kind::Bool` → `"Bool"`
   The `Keyword` case is the interesting one: the kind label is `"Keyword"` but the string payload lands in `col{c}_string`, NOT a new `col{c}_keyword` column. This keeps the tagged layout to exactly 6 sub-columns (we'd otherwise need 7). Document this in a comment at the kind-to-value-column dispatch.

8. **Homogeneous column special case.** If a column is homogeneous AND its kind is `Keyword`, the existing `build_column` path already handles it via `StringBuilder`. No change.

9. **The existing `emit_empty_parquet` path for output-only relations with no FactDB slot** uses positional placeholder `col{i}:utf8` columns. That path is untouched by Plan 6 — empty relations don't have tuples to inspect for heterogeneity. Plan 6 changes only the populated-tuples path.

10. **No evaluator or bridge changes.** `populate_effect_facts` from Plan 5 is untouched. The bridge still produces `(FormID, :Keyword, Value)` tuples into `skyrim/set` et al. The parquet sink now emits these properly instead of warning + skipping.

---

## File Map

### M1 — Heterogeneous detection + tagged emission

**Modified:**
- `extensions/parquet/src/snapshot_sink.cpp` — refactor kind detection; add `build_tagged_columns` helper; branch the main per-column emission loop.

**New tests:**
- `extensions/parquet/tests/test_tagged_columns.cpp` — construct a FactDB with a heterogeneous column; emit; read back via `parquet::arrow::FileReader`; assert the 6-field schema, per-row null/non-null placement, and typed value decoding.

### M2 — Effect-facts round-trip integration

**Modified:**
- `tests/cli/test_cli_parquet_sink.cpp` — add a new gtest case that wires the Plan 5 `populate_effect_facts` bridge into the existing CLI sink-dispatch mirror, with a synthetic PatchBuffer carrying Int + Float + FormID entries, and asserts the `skyrim/set.parquet` file contains the expected tagged-column rows.

**No new files in M2.** The CLI smoke-test in Task 12 is a manual verification step, not a committed test.

---

## Baseline

- [ ] **Step B1: Verify branch state**

```bash
cd /home/tbaldrid/oss/mora
git status
git log master..HEAD --oneline
```

Expected: clean tree; 14 commits; HEAD `7bdc8b0`.

- [ ] **Step B2: Verify build + tests**

```bash
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean; 90 binaries pass.

---

## Milestone 1 — Heterogeneous detection + tagged emission

Goal: per-column kind scan; for heterogeneous columns, emit 6 tagged Arrow fields instead of warn + skip. New unit test verifies the round-trip.

### Task 1: Refactor kind detection to be per-column

**File:** `extensions/parquet/src/snapshot_sink.cpp`

The current code at baseline (around lines 282–320) computes `kinds[]` from the first tuple, then does a two-pass check:
1. If any column's `arrow_type_for(kinds[c]) == nullptr` → unsupported (List/Var) → skip whole relation.
2. Walk all tuples; if any `t.size() != arity` OR `t[c].kind() != kinds[c]` → skip whole relation as heterogeneous.

Replace (2) with per-column kind-set building. The first-tuple `kinds[]` stays as "the reference kind for homogeneous columns."

- [ ] **Step 1.1: Replace the heterogeneity scan**

Open `extensions/parquet/src/snapshot_sink.cpp`. Find this block (at or near the line numbers noted — check the file for the exact location):

```cpp
        bool skip_heterogeneous = false;
        for (const auto& t : tuples) {
            if (t.size() != arity) { skip_heterogeneous = true; break; }
            for (std::size_t c = 0; c < arity; ++c) {
                if (t[c].kind() != kinds[c]) { skip_heterogeneous = true; break; }
            }
            if (skip_heterogeneous) break;
        }
        if (skip_heterogeneous) {
            ctx.diags.warning("parquet-skip-heterogeneous",
                fmt::format("parquet.snapshot: skipping relation '{}' — "
                            "tuples have inconsistent arity or per-column kinds",
                            rel_name),
                mora::SourceSpan{}, "");
            continue;
        }
```

Replace with a narrower check for arity only, plus per-column kind sets:

```cpp
        // Check arity consistency first — that's a hard shape error.
        bool arity_mismatch = false;
        for (const auto& t : tuples) {
            if (t.size() != arity) { arity_mismatch = true; break; }
        }
        if (arity_mismatch) {
            ctx.diags.warning("parquet-skip-arity-mismatch",
                fmt::format("parquet.snapshot: skipping relation '{}' — "
                            "tuples have inconsistent arity",
                            rel_name),
                mora::SourceSpan{}, "");
            continue;
        }

        // Per-column kind sets. A column with a single kind is
        // homogeneous (fast path via build_column). A column with
        // multiple kinds is heterogeneous — emit six tagged Arrow
        // fields for it in the schema-building step below.
        std::vector<std::unordered_set<mora::Value::Kind>> col_kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            for (const auto& t : tuples) {
                col_kinds[c].insert(t[c].kind());
            }
        }
```

Also add `#include <unordered_set>` at the top of the file if not already present (it is — `unordered_set` is used elsewhere in the file).

- [ ] **Step 1.2: Remove the now-redundant `kinds[]` initialization**

Earlier in the same block (just after the `const std::size_t arity = tuples.front().size();` line) there's:

```cpp
        std::vector<mora::Value::Kind> kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            kinds[c] = tuples.front()[c].kind();
        }
```

Leave this in place — it's still used by the unsupported-kind check immediately after. The unsupported-kind check needs updating too:

Find:

```cpp
        bool skip_unsupported = false;
        for (std::size_t c = 0; c < arity; ++c) {
            if (arrow_type_for(kinds[c]) == nullptr) {
                skip_unsupported = true;
                break;
            }
        }
```

Change to iterate per-column kind sets (since List/Var can appear in a heterogeneous column that's otherwise fine):

```cpp
        // Check every kind in every column for unsupported (List/Var).
        // This runs BEFORE the per-column kind-set build above would
        // make sense, so we do it lazily: compute the kind sets first,
        // then scan them.
```

This is awkward — better to compute `col_kinds[c]` BEFORE the unsupported-kind check, since the check needs to see all kinds across all tuples, not just the first tuple's kinds.

Re-order the block so the per-column kind-set computation happens first (right after the arity check), then the unsupported-kind check uses `col_kinds`, then the homogeneous-vs-heterogeneous branching happens in the schema build.

The corrected order:

```cpp
        // 1. Arity check
        bool arity_mismatch = false;
        for (const auto& t : tuples) {
            if (t.size() != arity) { arity_mismatch = true; break; }
        }
        if (arity_mismatch) { ... warn + continue ... }

        // 2. Per-column kind sets
        std::vector<std::unordered_set<mora::Value::Kind>> col_kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            for (const auto& t : tuples) {
                col_kinds[c].insert(t[c].kind());
            }
        }

        // 3. Unsupported-kind check (uses col_kinds)
        bool skip_unsupported = false;
        for (std::size_t c = 0; c < arity && !skip_unsupported; ++c) {
            for (auto k : col_kinds[c]) {
                if (k == mora::Value::Kind::Var || k == mora::Value::Kind::List) {
                    skip_unsupported = true;
                    break;
                }
            }
        }
        if (skip_unsupported) {
            ctx.diags.warning("parquet-skip-unsupported-kind",
                fmt::format("parquet.snapshot: skipping relation '{}' — "
                            "column contains List or Var value (not supported in v1)",
                            rel_name),
                mora::SourceSpan{}, "");
            continue;
        }
```

Delete the now-unused initial `kinds[]` computation (the `std::vector<mora::Value::Kind> kinds(arity);` + its loop). The `kinds[]` vector is replaced by `col_kinds[c]` which carries strictly more information.

- [ ] **Step 1.3: Build to confirm no behavior regression yet**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_parquet 2>&1 | tail -5
xmake test 2>&1 | tail -3
```

Expected: clean build; 90 binaries pass. At this point the per-column scan exists but we haven't changed emission — heterogeneous columns still fail the existing schema build because the homogeneous-only path is still in place. However, the test suite doesn't currently exercise heterogeneous relations, so this step should stay green.

If any test fails, something other than heterogeneity was previously triggering the `parquet-skip-heterogeneous` code path — investigate before proceeding.

### Task 2: Add the `build_tagged_columns` helper

**File:** `extensions/parquet/src/snapshot_sink.cpp` (same file)

Add a new helper in the anonymous namespace at the top of the file, alongside `arrow_type_for`, `build_column`, and `emit_empty_parquet`.

- [ ] **Step 2.1: Add the helper function**

After the existing `build_column` function definition (near line ~107 at baseline), add:

```cpp
// Names used in the `_kind` string column to identify which typed
// sub-column holds the row's value. Keep this list in sync with
// Value::Kind.
const char* kind_name_for(mora::Value::Kind k) {
    switch (k) {
        case mora::Value::Kind::FormID:  return "FormID";
        case mora::Value::Kind::Int:     return "Int";
        case mora::Value::Kind::Float:   return "Float";
        case mora::Value::Kind::String:  return "String";
        case mora::Value::Kind::Keyword: return "Keyword";
        case mora::Value::Kind::Bool:    return "Bool";
        case mora::Value::Kind::Var:
        case mora::Value::Kind::List:    return "Unsupported";
    }
    return "Unknown";
}

// Build the six Arrow arrays that make up a tagged-column group for a
// heterogeneous column at position `col`. Order: kind, formid, int,
// float, string, bool. Exactly one of the last five is non-null per
// row, selected by the kind. `Keyword` values land in `_string` with
// the kind tag set to "Keyword" (Plan 6 uses six sub-columns, not
// seven — keyword payload is an interned string either way).
//
// Precondition: every tuple has at least `col + 1` values. Only Bool /
// FormID / Int / Float / String / Keyword kinds are expected here —
// unsupported kinds (Var/List) trigger a relation-level skip before
// this function is called.
arrow::Result<std::vector<std::shared_ptr<arrow::Array>>>
build_tagged_columns(const std::vector<mora::Tuple>& tuples,
                     std::size_t col,
                     const mora::StringPool& pool) {
    arrow::StringBuilder  b_kind;
    arrow::UInt32Builder  b_formid;
    arrow::Int64Builder   b_int;
    arrow::DoubleBuilder  b_float;
    arrow::StringBuilder  b_string;
    arrow::BooleanBuilder b_bool;

    ARROW_RETURN_NOT_OK(b_kind.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_formid.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_int.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_float.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_string.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_bool.Reserve(tuples.size()));

    for (const auto& t : tuples) {
        const auto& v = t[col];
        const auto k = v.kind();
        const auto* name = kind_name_for(k);
        ARROW_RETURN_NOT_OK(b_kind.Append(name, std::strlen(name)));

        // Set the matching typed column; AppendNull() on all the others.
        switch (k) {
            case mora::Value::Kind::FormID: {
                b_formid.UnsafeAppend(v.as_formid());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Int: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                b_int.UnsafeAppend(v.as_int());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Float: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                b_float.UnsafeAppend(v.as_float());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::String: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                auto sv = pool.get(v.as_string());
                ARROW_RETURN_NOT_OK(b_string.Append(sv.data(), sv.size()));
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Keyword: {
                // Keyword payload shares the _string column. The _kind
                // tag is "Keyword" so consumers can tell them apart.
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                auto sv = pool.get(v.as_keyword());
                ARROW_RETURN_NOT_OK(b_string.Append(sv.data(), sv.size()));
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Bool: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                b_bool.UnsafeAppend(v.as_bool());
                break;
            }
            case mora::Value::Kind::Var:
            case mora::Value::Kind::List:
                // Already filtered by the unsupported-kind check upstream.
                return arrow::Status::Invalid(
                    "build_tagged_columns: unsupported kind reached "
                    "(Var/List); upstream filter missed one");
        }
    }

    std::vector<std::shared_ptr<arrow::Array>> out;
    out.reserve(6);
    for (auto* builder : std::array<arrow::ArrayBuilder*, 6>{
             &b_kind, &b_formid, &b_int, &b_float, &b_string, &b_bool}) {
        std::shared_ptr<arrow::Array> arr;
        ARROW_RETURN_NOT_OK(builder->Finish(&arr));
        out.push_back(std::move(arr));
    }
    return out;
}
```

Add `#include <cstring>` for `std::strlen` and `#include <array>` if not already present.

- [ ] **Step 2.2: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_parquet 2>&1 | tail -5
```

Expected: clean build. The helper is defined but not called yet.

### Task 3: Wire the main emission loop to branch per-column

**File:** `extensions/parquet/src/snapshot_sink.cpp` (same file)

Find the current schema-and-columns build block (around lines 322–344 at baseline):

```cpp
        // Build Arrow schema + columns.
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> columns;
        fields.reserve(arity);
        columns.reserve(arity);

        bool column_failed = false;
        for (std::size_t c = 0; c < arity; ++c) {
            fields.push_back(arrow::field(fmt::format("col{}", c),
                                           arrow_type_for(kinds[c])));
            auto col = build_column(tuples, c, kinds[c], ctx.pool);
            if (!col.ok()) {
                ctx.diags.error("parquet-build-column",
                    fmt::format("parquet.snapshot: failed to build column {} "
                                "of relation '{}': {}",
                                c, rel_name, col.status().ToString()),
                    mora::SourceSpan{}, "");
                column_failed = true;
                break;
            }
            columns.push_back(*col);
        }
        if (column_failed) continue;
```

Replace the inner per-column work to branch on homogeneity:

```cpp
        // Build Arrow schema + columns. For each input column:
        //  - Homogeneous (col_kinds[c].size() == 1): emit one typed
        //    field named "col{c}" via build_column.
        //  - Heterogeneous: expand into six tagged sub-columns named
        //    "col{c}_kind", "col{c}_formid", "col{c}_int",
        //    "col{c}_float", "col{c}_string", "col{c}_bool".
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> columns;

        bool column_failed = false;
        for (std::size_t c = 0; c < arity; ++c) {
            if (col_kinds[c].size() == 1) {
                // Homogeneous — single typed column.
                const auto k = *col_kinds[c].begin();
                fields.push_back(arrow::field(fmt::format("col{}", c),
                                               arrow_type_for(k)));
                auto col = build_column(tuples, c, k, ctx.pool);
                if (!col.ok()) {
                    ctx.diags.error("parquet-build-column",
                        fmt::format("parquet.snapshot: failed to build "
                                    "column {} of relation '{}': {}",
                                    c, rel_name, col.status().ToString()),
                        mora::SourceSpan{}, "");
                    column_failed = true;
                    break;
                }
                columns.push_back(*col);
            } else {
                // Heterogeneous — six tagged sub-columns.
                fields.push_back(arrow::field(
                    fmt::format("col{}_kind",    c), arrow::utf8()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_formid",  c), arrow::uint32()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_int",     c), arrow::int64()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_float",   c), arrow::float64()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_string",  c), arrow::utf8()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_bool",    c), arrow::boolean()));
                auto tagged = build_tagged_columns(tuples, c, ctx.pool);
                if (!tagged.ok()) {
                    ctx.diags.error("parquet-build-column",
                        fmt::format("parquet.snapshot: failed to build "
                                    "tagged column {} of relation '{}': {}",
                                    c, rel_name, tagged.status().ToString()),
                        mora::SourceSpan{}, "");
                    column_failed = true;
                    break;
                }
                for (auto& arr : *tagged) {
                    columns.push_back(std::move(arr));
                }
            }
        }
        if (column_failed) continue;
```

Remove the `fields.reserve(arity)` / `columns.reserve(arity)` lines — with tagged expansion, the final count isn't known up front. Leaving `reserve` unset is fine; Arrow's builder vectors are small.

- [ ] **Step 3.1: Build + run existing tests**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean; 90 binaries pass. Existing tests all exercise homogeneous relations, so they should continue to work via the size-1 branch.

### Task 4: Unit test — heterogeneous round-trip

**File:** `extensions/parquet/tests/test_tagged_columns.cpp` (NEW)

- [ ] **Step 4.1: Create the test**

```cpp
#include "mora_parquet/snapshot_sink.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir(const std::string& label) {
    auto root = fs::temp_directory_path() /
                ("mora-tagged-" + label + "-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// Helper: read a parquet file into an arrow::Table.
std::shared_ptr<arrow::Table> read_table(const fs::path& path) {
    auto infile = *arrow::io::ReadableFile::Open(path.string());
    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto open = parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader);
    EXPECT_TRUE(open.ok()) << open.ToString();
    std::shared_ptr<arrow::Table> table;
    EXPECT_TRUE(reader->ReadTable(&table).ok());
    return table;
}

TEST(ParquetTaggedColumns, HeterogeneousValueColumnEmitsSixSubColumns) {
    // Relation: effect/set(entity: FormID, field: Keyword, value: ANY)
    // Value column is heterogeneous across rows: Int, Float, String, FormID.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("effect/set");
    db.configure_relation(rel, /*arity*/ 3, /*indexed*/ {0});

    db.add_fact(rel, {
        mora::Value::make_formid(0x0100),
        mora::Value::make_keyword(pool.intern("GoldValue")),
        mora::Value::make_int(100),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0101),
        mora::Value::make_keyword(pool.intern("Damage")),
        mora::Value::make_float(12.5),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0102),
        mora::Value::make_keyword(pool.intern("Name")),
        mora::Value::make_string(pool.intern("Skeever")),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0103),
        mora::Value::make_keyword(pool.intern("Race")),
        mora::Value::make_formid(0x01337F),
    });

    auto out_dir = make_temp_dir("roundtrip");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    auto file = out_dir / "effect" / "set.parquet";
    ASSERT_TRUE(fs::exists(file)) << file.string();

    auto table = read_table(file);
    ASSERT_NE(table, nullptr);

    // Expected fields: col0 (uint32), col1 (utf8),
    // col2_kind (utf8), col2_formid (uint32),
    // col2_int (int64), col2_float (float64),
    // col2_string (utf8), col2_bool (bool).
    ASSERT_EQ(table->num_columns(), 8);
    EXPECT_EQ(table->schema()->field(0)->name(), "col0");
    EXPECT_EQ(table->schema()->field(1)->name(), "col1");
    EXPECT_EQ(table->schema()->field(2)->name(), "col2_kind");
    EXPECT_EQ(table->schema()->field(3)->name(), "col2_formid");
    EXPECT_EQ(table->schema()->field(4)->name(), "col2_int");
    EXPECT_EQ(table->schema()->field(5)->name(), "col2_float");
    EXPECT_EQ(table->schema()->field(6)->name(), "col2_string");
    EXPECT_EQ(table->schema()->field(7)->name(), "col2_bool");

    ASSERT_EQ(table->num_rows(), 4);

    auto kind_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(2)->chunk(0));
    auto formid_col = std::static_pointer_cast<arrow::UInt32Array>(
        table->column(3)->chunk(0));
    auto int_col = std::static_pointer_cast<arrow::Int64Array>(
        table->column(4)->chunk(0));
    auto float_col = std::static_pointer_cast<arrow::DoubleArray>(
        table->column(5)->chunk(0));
    auto string_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
    auto bool_col = std::static_pointer_cast<arrow::BooleanArray>(
        table->column(7)->chunk(0));

    // Row 0: Int 100
    EXPECT_EQ(kind_col->GetString(0), "Int");
    EXPECT_TRUE(formid_col->IsNull(0));
    EXPECT_FALSE(int_col->IsNull(0));
    EXPECT_EQ(int_col->Value(0), 100);
    EXPECT_TRUE(float_col->IsNull(0));
    EXPECT_TRUE(string_col->IsNull(0));
    EXPECT_TRUE(bool_col->IsNull(0));

    // Row 1: Float 12.5
    EXPECT_EQ(kind_col->GetString(1), "Float");
    EXPECT_TRUE(formid_col->IsNull(1));
    EXPECT_TRUE(int_col->IsNull(1));
    EXPECT_FALSE(float_col->IsNull(1));
    EXPECT_DOUBLE_EQ(float_col->Value(1), 12.5);
    EXPECT_TRUE(string_col->IsNull(1));
    EXPECT_TRUE(bool_col->IsNull(1));

    // Row 2: String "Skeever"
    EXPECT_EQ(kind_col->GetString(2), "String");
    EXPECT_TRUE(formid_col->IsNull(2));
    EXPECT_TRUE(int_col->IsNull(2));
    EXPECT_TRUE(float_col->IsNull(2));
    EXPECT_FALSE(string_col->IsNull(2));
    EXPECT_EQ(string_col->GetString(2), "Skeever");
    EXPECT_TRUE(bool_col->IsNull(2));

    // Row 3: FormID 0x01337F
    EXPECT_EQ(kind_col->GetString(3), "FormID");
    EXPECT_FALSE(formid_col->IsNull(3));
    EXPECT_EQ(formid_col->Value(3), 0x01337Fu);
    EXPECT_TRUE(int_col->IsNull(3));
    EXPECT_TRUE(float_col->IsNull(3));
    EXPECT_TRUE(string_col->IsNull(3));
    EXPECT_TRUE(bool_col->IsNull(3));
}

TEST(ParquetTaggedColumns, HomogeneousColumnStaysTyped) {
    // Regression: ensure the homogeneous fast path still emits a single
    // typed column named "col0" (NOT "col0_kind" etc.) when every tuple
    // in a column shares the same kind.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("form/npc");
    db.configure_relation(rel, /*arity*/ 2, /*indexed*/ {0});

    db.add_fact(rel, {
        mora::Value::make_formid(0x0100),
        mora::Value::make_string(pool.intern("Alice")),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0101),
        mora::Value::make_string(pool.intern("Bob")),
    });

    auto out_dir = make_temp_dir("homogeneous");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors());
    auto table = read_table(out_dir / "form" / "npc.parquet");

    ASSERT_EQ(table->num_columns(), 2);
    EXPECT_EQ(table->schema()->field(0)->name(), "col0");
    EXPECT_EQ(table->schema()->field(0)->type()->id(), arrow::Type::UINT32);
    EXPECT_EQ(table->schema()->field(1)->name(), "col1");
    EXPECT_EQ(table->schema()->field(1)->type()->id(), arrow::Type::STRING);
}

TEST(ParquetTaggedColumns, KeywordInHeterogeneousColumnTaggedAsKeyword) {
    // Special case: Value::Kind::Keyword shares storage with String but
    // is a distinct kind. In a tagged column the _kind tag must be
    // "Keyword" while the payload lands in _string. Consumers rely on
    // the kind tag to tell them apart.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("kw/test");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {});

    db.add_fact(rel, {mora::Value::make_string(pool.intern("plain"))});
    db.add_fact(rel, {mora::Value::make_keyword(pool.intern("tagged"))});

    auto out_dir = make_temp_dir("keyword");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors());
    auto table = read_table(out_dir / "kw" / "test.parquet");

    ASSERT_EQ(table->num_columns(), 6);   // 6 tagged sub-columns, no base col
    EXPECT_EQ(table->schema()->field(0)->name(), "col0_kind");
    EXPECT_EQ(table->schema()->field(4)->name(), "col0_string");

    auto kind_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(0)->chunk(0));
    auto string_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(4)->chunk(0));

    EXPECT_EQ(kind_col->GetString(0), "String");
    EXPECT_EQ(string_col->GetString(0), "plain");

    EXPECT_EQ(kind_col->GetString(1), "Keyword");
    EXPECT_EQ(string_col->GetString(1), "tagged");
}

} // namespace
```

- [ ] **Step 4.2: Reconfigure xmake to pick up the new test**

```bash
cd /home/tbaldrid/oss/mora
xmake f -p linux -m debug --yes 2>&1 | tail -3
```

- [ ] **Step 4.3: Build + run the new test**

```bash
xmake build test_tagged_columns 2>&1 | tail -3
./build/linux/x86_64/debug/test_tagged_columns 2>&1 | tail -20
```

Expected: 3 cases PASS.

- [ ] **Step 4.4: Full suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 91` (90 prior + new test_tagged_columns).

### Task 5: Commit M1

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: parquet tagged-column encoding for heterogeneous columns

Replaces the parquet sink's per-relation heterogeneity skip with
per-column heterogeneity detection, emitting six Arrow sub-columns
for each heterogeneous column:

  col{c}_kind    — utf8, the kind name per row
  col{c}_formid  — uint32, nullable
  col{c}_int     — int64,  nullable
  col{c}_float   — float64, nullable
  col{c}_string  — utf8,   nullable (also stores Keyword payload)
  col{c}_bool    — bool,   nullable

Exactly one of the five typed columns is non-null per row, selected
by the kind tag. Keyword values carry the kind tag "Keyword" but
land in the _string column (six sub-columns, not seven).

Homogeneous columns keep the original single-typed-field encoding
via build_column. The detection is per-column, so a relation with
(homogeneous FormID, homogeneous Keyword, heterogeneous Any) still
emits col0/col1 as typed columns and expands only col2.

The former parquet-skip-heterogeneous warning is retired (the only
remaining skip-whole-relation cases are arity mismatch — renamed to
parquet-skip-arity-mismatch — and List/Var values, which still
trigger parquet-skip-unsupported-kind).

The effect relations populated by Plan 5's populate_effect_facts
bridge (skyrim/set et al., where the value column is heterogeneous
by design) now actually reach parquet. The runtime consumer can
dispatch on col2_kind to extract the typed value.

Tests:
  * extensions/parquet/tests/test_tagged_columns.cpp (new):
    - HeterogeneousValueColumnEmitsSixSubColumns — builds a
      3-column relation with Int/Float/String/FormID values in the
      third column; reads back; asserts 8-column schema + per-row
      null/non-null placement.
    - HomogeneousColumnStaysTyped — regression check that the
      single-typed-field path still applies when all tuples agree
      on kind.
    - KeywordInHeterogeneousColumnTaggedAsKeyword — verifies the
      Keyword-via-_string encoding with a "Keyword" kind tag.

Part 6 of the v3 rewrite (milestone 1 of 2 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Include the untracked plan doc at `docs/superpowers/plans/2026-04-17-mora-v3-plan-6-parquet-tagged-columns.md` in this commit (`git add -A` handles it).

---

## Milestone 2 — Effect-facts round-trip integration

Goal: verify the Plan 5 bridge → Plan 6 sink path end-to-end by emitting real effect facts through parquet and reading them back.

### Task 6: Add a bridge-to-parquet round-trip test case

**File:** `tests/cli/test_cli_parquet_sink.cpp`

The existing test file has three cases from Plan 3 + Plan 4. Add a fourth case that combines the Plan 5 bridge with the Plan 6 sink.

- [ ] **Step 6.1: Add includes + case**

At the top of `tests/cli/test_cli_parquet_sink.cpp`, ensure these includes are present (add any missing):

```cpp
#include "mora/emit/patch_table.h"       // PatchValueType
#include "mora/eval/effect_facts.h"      // populate_effect_facts
#include "mora/eval/patch_buffer.h"
#include "mora/eval/patch_set.h"         // FieldOp
#include "mora/model/relations.h"        // FieldId

#include <arrow/api.h>
#include <parquet/arrow/reader.h>
```

Most of these are already present from the prior cases — add only what's missing.

Then append inside the anonymous namespace, before the closing `} // namespace`:

```cpp
TEST(CliParquetSink, EffectFactsBridgeRoundTripThroughTaggedColumns) {
    // Builds a synthetic PatchBuffer with three different value types
    // (Int, Float, FormID), runs the Plan 5 bridge to populate the
    // skyrim/set FactDB relation, dispatches the parquet sink with
    // output-only, then reads skyrim/set.parquet back and verifies the
    // tagged-column layout carries the three rows with correct kinds +
    // values.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);
    mora_parquet::register_parquet(ctx);

    // Build a synthetic PatchBuffer with three entries, one per
    // supported value type.
    mora::PatchBuffer buf;
    auto const form = uint32_t{0x000ABCDE};

    buf.emit(form,
             static_cast<uint8_t>(mora::FieldId::GoldValue),
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Int),
             /*value*/ 750);

    double const weight = 2.5;
    buf.emit(form,
             static_cast<uint8_t>(mora::FieldId::Weight),
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::Float),
             std::bit_cast<uint64_t>(weight));

    buf.emit(form,
             static_cast<uint8_t>(mora::FieldId::Race),
             static_cast<uint8_t>(mora::FieldOp::Set),
             static_cast<uint8_t>(mora::PatchValueType::FormID),
             /*value*/ uint64_t{0x01337F});

    // Populate skyrim/set via the bridge.
    mora::populate_effect_facts(buf, db, pool);

    // Dispatch the parquet sink.
    auto out_dir = fs::temp_directory_path() /
                   ("mora-bridge-roundtrip-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    std::unordered_map<std::string, std::string> sink_configs = {
        {"parquet.snapshot", out_dir.string() + "?output-only"},
    };
    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx ectx{pool, diags, it->second, &ctx};
        sink->emit(ectx, db);
    }

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    auto file = out_dir / "skyrim" / "set.parquet";
    ASSERT_TRUE(fs::exists(file)) << file.string();

    // Read back and verify. skyrim/set schema: (FormID, Keyword, ANY).
    // Columns 0 and 1 are homogeneous; column 2 expands into six
    // tagged sub-columns. Expected Arrow fields:
    //   col0 uint32, col1 utf8,
    //   col2_kind utf8, col2_formid uint32, col2_int int64,
    //   col2_float float64, col2_string utf8, col2_bool bool.
    auto infile = *arrow::io::ReadableFile::Open(file.string());
    std::unique_ptr<parquet::arrow::FileReader> reader;
    ASSERT_TRUE(parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader).ok());
    std::shared_ptr<arrow::Table> table;
    ASSERT_TRUE(reader->ReadTable(&table).ok());

    ASSERT_EQ(table->num_columns(), 8);
    ASSERT_EQ(table->num_rows(), 3);

    auto kind_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(2)->chunk(0));
    auto formid_col = std::static_pointer_cast<arrow::UInt32Array>(
        table->column(3)->chunk(0));
    auto int_col = std::static_pointer_cast<arrow::Int64Array>(
        table->column(4)->chunk(0));
    auto float_col = std::static_pointer_cast<arrow::DoubleArray>(
        table->column(5)->chunk(0));

    // Order depends on PatchBuffer iteration (sort_and_dedup hasn't run,
    // so insertion order). Scan all rows and match by the field-keyword
    // column rather than asserting a specific row order — robust to
    // future sort changes in PatchBuffer.
    auto field_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(1)->chunk(0));

    bool seen_int = false, seen_float = false, seen_formid = false;
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
            EXPECT_FALSE(formid_col->IsNull(i));
            EXPECT_EQ(formid_col->Value(i), 0x01337Fu);
            seen_formid = true;
        }
    }

    EXPECT_TRUE(seen_int);
    EXPECT_TRUE(seen_float);
    EXPECT_TRUE(seen_formid);
}
```

Ensure `#include <bit>` is present near the top of the file (for `std::bit_cast`).

- [ ] **Step 6.2: Reconfigure xmake**

```bash
cd /home/tbaldrid/oss/mora
xmake f -p linux -m debug --yes 2>&1 | tail -2
```

- [ ] **Step 6.3: Build + run**

```bash
xmake build test_cli_parquet_sink 2>&1 | tail -3
./build/linux/x86_64/debug/test_cli_parquet_sink 2>&1 | tail -15
```

Expected: 4 cases pass (the 3 prior + 1 new).

### Task 7: CLI regression smoke test

Manual verification that existing CLI behavior is preserved.

- [ ] **Step 7.1: Rebuild release**

```bash
cd /home/tbaldrid/oss/mora
xmake f -p linux -m release --yes
xmake build
xmake f -p linux -m debug --yes
```

(Release build is needed for the smoke test; restore debug config afterward.)

- [ ] **Step 7.2: Run the Plan 4/5 smoke test**

```bash
cd /tmp && rm -rf mora-p6-smoke && mkdir -p mora-p6-smoke && cd mora-p6-smoke
echo "namespace smoke" > empty.mora
/home/tbaldrid/oss/mora/build/linux/x86_64/release/mora compile empty.mora \
    --data-dir /tmp/mora-p6-smoke \
    --sink "parquet.snapshot=./parq?output-only"
echo "exit=$?"
ls -R parq 2>&1 | head -10
```

Expected: exit 0. `parq/skyrim/{set,add,remove,multiply}.parquet` — four files. Since the `.mora` source produces no effect facts, the output-only empty-file path applies: these files should be the shape-only zero-row parquets with the `emit_empty_parquet` helper's utf8 placeholder columns. This path is unchanged by Plan 6.

The meaningful round-trip through tagged columns lives in Task 6's unit test (which populates skyrim/set programmatically). A true end-to-end `mora compile` that populates these relations requires real rules + ESP data and is out of scope for Plan 6.

### Task 8: Final build + test gate

```bash
cd /home/tbaldrid/oss/mora
xmake clean -a
xmake f -p linux -m debug --yes
xmake build 2>&1 | tail -5
xmake test 2>&1 | tail -3
```

Expected: clean build; `100% tests passed, 0 test(s) failed out of 91`.

### Task 9: Commit M2

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: effect-facts → tagged-column parquet round-trip test

End-to-end verification that the Plan 5 populate_effect_facts
bridge + the Plan 6 tagged-column parquet encoding together land
real effect facts on disk in a readable shape.

tests/cli/test_cli_parquet_sink.cpp adds one new gtest case:
EffectFactsBridgeRoundTripThroughTaggedColumns. It constructs a
synthetic PatchBuffer with three entries spanning Int, Float, and
FormID PatchValueTypes; runs populate_effect_facts to materialise
them into the skyrim/set FactDB relation; dispatches the parquet
sink with output-only; reads skyrim/set.parquet back via
parquet::arrow::FileReader; and verifies the 8-column tagged
schema plus per-row kind tags and non-null value-column
placement.

No production-code changes in M2 — this is purely the integration
test that proves the two milestones compose correctly.

CLI smoke (mora compile with --sink parquet.snapshot=?output-only)
continues to produce the four empty shape-only parquet files for
skyrim/{set,add,remove,multiply}, since production rules don't
exercise the bridge end-to-end yet. That path is unchanged.

Part 6 of the v3 rewrite (milestone 2 of 2 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Done

After Task 9, Plan 6 is complete. Branch state:
- 16 commits on `mora-v3-foundation` (3 P1 + 3 P2 + 3 P3 + 3 P4 + 2 P5 + 2 P6).
- `xmake build` green.
- 91 test binaries pass (90 prior + new test_tagged_columns).
- Parquet sink now emits real data for heterogeneous relations; effect facts populated by the Plan 5 bridge round-trip through parquet.

**Deferred to later plans:**
- Delete `src/emit/` once the evaluator stops producing `PatchBuffer` (Plan 7+).
- Teach the evaluator to produce facts into `skyrim/set` directly, retiring the bridge (Plan 7+).
- Plumb the string_table through so `PatchValueType::StringIndex` can be decoded into a proper `Value::Kind::String` (currently skipped).
- Fix `FieldId::RaceForm → "Race(form)"` keyword-name round-trip issue (cosmetic).
- Retire the legacy `:Symbol → FormID` evaluator fallback.
- Move `data/relations/**/*.yaml` into the Skyrim extension.

**What's next (Plan 7 — not in this plan):**
Most natural candidates:
1. **Route the effect-facts bridge through PatchSet instead of PatchBuffer** so `PatchValueType::StringIndex` can decode into real strings. That plus (2) lets Plan 8 delete `src/emit/`.
2. **Teach the evaluator to produce effect facts directly** (instead of via PatchBuffer + bridge). Once that's done, `src/emit/` becomes unused.
3. **Delete `src/emit/`** — only viable after (1) + (2).
