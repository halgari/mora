# Mora v3 — Plan 3: Parquet Sink + Sink API

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the `Sink` extension point and a working `ParquetSnapshotSink` that writes `FactDB` relations to a directory of parquet files. After this plan, `mora compile --sink parquet.snapshot=./out` dumps the derived FactDB to `out/<namespace>/<relation>.parquet` alongside the existing `mora_patches.bin` binary output. The binary patch writer is untouched — parquet is a second, opt-in output channel.

**Architecture:** Three-milestone layered change. M1 fixes a Plan 2 review carry-over, introduces the `Sink` interface, and wires `register_sink` into `ExtensionContext`. M2 builds the `mora_parquet` extension target on Apache Arrow and implements `ParquetSnapshotSink` with a roundtrip unit test. M3 wires the sink into the CLI via a `--sink` flag and adds a sink-integration test. No changes to `src/emit/` or the evaluator in this plan.

**Tech Stack:** C++20, xmake (package: `arrow` 7.0.0+ from xmake-repo — provides libarrow and libparquet), gtest.

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md` (step 5 of the "Order of operations"). Spec step 6 (delete `src/emit/`) is explicitly **NOT** in this plan — it depends on evaluator changes that come in later plans.

**Branch:** continue on `mora-v3-foundation`. Plan 3 layers three commits on top of the existing six.

**Baseline:** HEAD `0534f51` (P2 M3). Clean tree. `xmake build` green. 85 tests pass.

---

## Known constraints and design notes

1. **`FactDB` is dynamically typed at the column level.** A `Tuple` is `std::vector<Value>` where each `Value` is a tagged variant (`Var / FormID / Int / Float / String / Bool / List`). Nothing in the type system prevents different tuples in the same relation from having different kinds in the same column position; in practice sema enforces homogeneity, but the emitter must not assume it.

    **Sink policy:** use the first tuple in a relation to infer each column's kind. If a later tuple mismatches that kind in the same column, emit a diagnostic and skip that relation. Do NOT try to coerce or upcast.

2. **`List` and `Var` values are not persistable in v1.** `Var` is a planner placeholder, never stored; if a tuple somehow contains one, emit a diagnostic and skip. `List` columns are out of scope for Plan 3 — skip relations that contain any `List` value and emit an info-level diagnostic naming them.

3. **Relation name lookup:** `FactDB` keys relations by `uint32_t` StringId index. The sink needs human-readable names to build file paths and Arrow column names. Add a public accessor `FactDB::all_relation_names()` that returns a `std::vector<StringId>` of all relations; the caller already has access to a `StringPool&` (via the sink's `EmitCtx`) to resolve each id to a `std::string_view`.

4. **File layout:** for a relation named `form/npc`, the parquet path is `<out-dir>/form/npc.parquet`. For `plugin_exists`, it's `<out-dir>/plugin_exists.parquet` (no subdir). Create intermediate dirs as needed.

5. **Arrow type mapping:**
   | mora `Value::Kind` | Arrow type | Parquet logical type |
   |---|---|---|
   | FormID | `uint32` | `INT_32 UNSIGNED` |
   | Int    | `int64`  | `INT_64` |
   | Float  | `float64`| `DOUBLE` |
   | String | `utf8`   | `STRING` (via StringPool resolution) |
   | Bool   | `boolean`| `BOOLEAN` |

    Column names: `col0`, `col1`, … (no schema metadata yet — relations don't have named columns at the `FactDB` level in Plan 3; Plan 5 adds that).

---

## File Map

### M1 — Pre-req fix + Sink API

**Modified:**
- `src/ext/extension.cpp` — snapshot `ctx.diags.has_errors()` before the collision pass, compare after, so `load_required` only bails on collisions it emitted itself (fixes Plan 2 reviewer's Important #1).
- `include/mora/ext/sink.h` — replace forward-declaration stub with the real `Sink` + `EmitCtx` definitions.
- `include/mora/ext/extension.h` — add `register_sink` method + `sinks()` accessor.
- `src/ext/extension.cpp` — implement the Sink registration machinery (pimpl-additive).
- `tests/ext/test_extension.cpp` — add two gtest cases: `SinkRegistrationPreservesOrder`, `SinksAccessorReturnsRegisteredSinks`.

### M2 — Parquet extension + `ParquetSnapshotSink`

**Modified:**
- `extensions/parquet/xmake.lua` — add `arrow` package dep; add the new source file.
- `include/mora/eval/fact_db.h` + `src/eval/fact_db.cpp` — add `std::vector<StringId> all_relation_names() const`.

**New:**
- `extensions/parquet/include/mora_parquet/snapshot_sink.h` — class declaration.
- `extensions/parquet/src/snapshot_sink.cpp` — implementation.
- `extensions/parquet/src/register.cpp` — rewrite from no-op stub to register a `ParquetSnapshotSink` on the `ExtensionContext`.
- `extensions/parquet/tests/test_snapshot_sink_roundtrip.cpp` — build a FactDB with known content, emit parquet, read back with Arrow's parquet reader, assert content matches.

### M3 — CLI wiring + sink-integration test

**Modified:**
- `src/main.cpp` — parse `--sink` (repeatable, form `<name>=<path>`); construct the sinks map in the compile pipeline; call `register_parquet(ctx)` alongside `register_skyrim(ctx)`; after evaluation, dispatch each configured sink by name.
- `xmake.lua` — link `mora_parquet` into the `mora` binary (`add_deps("mora_lib", "mora_skyrim_compile", "mora_parquet")`).

**New:**
- `tests/cli/test_cli_parquet_sink.cpp` — integration test invoking the compile pipeline with a programmatically-built FactDB and confirming the emitted parquet files parse correctly.

---

## Baseline

- [ ] **Step B1: Confirm branch state**

```bash
cd /home/tbaldrid/oss/mora
git status
git log --oneline master..HEAD
```

Expected: clean tree; 6 commits on `mora-v3-foundation`, HEAD is `0534f51`.

- [ ] **Step B2: Confirm build + tests**

```bash
xmake f -p linux -m debug
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean build; `100% tests passed, 0 test(s) failed out of 85`.

---

## Milestone 1 — Pre-req fix + Sink API

Goal: fix the `load_required` error-sensitivity bug, then replace the `Sink` stub with the real interface and wire `register_sink` into `ExtensionContext`. Adds 2 gtest cases → 87 test binaries.

### Task 1: Fix `load_required` to bail only on collisions it emitted

**Files:** `src/ext/extension.cpp`

Current behavior: `load_required` emits collision diagnostics, then bails if `ctx.diags.has_errors()` is true — which is true even if the caller had emitted errors in an earlier phase. Fix: snapshot error count before the collision loop, bail only if the count grew.

- [ ] **Step 1.1: Edit `src/ext/extension.cpp`**

Inside `load_required`, replace:

```cpp
    if (ctx.diags.has_errors()) return 0;
```

with:

```cpp
    if (ctx.diags.error_count() > errors_before) return 0;
```

Add the snapshot at the top of the function, before the collision loop:

```cpp
std::size_t ExtensionContext::load_required(LoadCtx& ctx, FactDB& out) const {
    const auto errors_before = ctx.diags.error_count();

    // ... collision loop unchanged ...

    if (ctx.diags.error_count() > errors_before) return 0;

    for (auto* src : matching) {
        src->load(ctx, out);
    }
    return matching.size();
}
```

`error_count()` already exists (`include/mora/diag/diagnostic.h:32`).

- [ ] **Step 1.2: Extend the duplicate-provides test to pre-seed an error**

Edit `tests/ext/test_extension.cpp`. Rename `DuplicateProvidesEmitsDiagnostic` to `DuplicateProvidesEmitsDiagnostic_GivenEmptyBag` (clarifies the existing scope). Then add a new case right after it:

```cpp
TEST(ExtensionContext, LoadRequiredToleratesPreexistingErrorsInDiagBag) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Simulate an earlier pipeline stage having already emitted an
    // error (e.g. sema reported a type mismatch). load_required must
    // still invoke matching sources without treating that pre-existing
    // error as a collision.
    diags.error("preexisting", "earlier-stage error",
                mora::SourceSpan{}, "");
    ASSERT_TRUE(diags.has_errors());

    mora::ext::ExtensionContext ec;
    std::size_t counter_a = 0;
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<std::string>{"rel.one"}, &counter_a));

    auto id_one = pool.intern("rel.one").index;
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, {id_one}};
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 1U);
    EXPECT_EQ(counter_a, 1U);
}
```

- [ ] **Step 1.3: Run the test binary directly**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_extension 2>&1 | tail -3
./build/linux/x86_64/debug/test_extension 2>&1 | tail -10
```

Expected: all 5 test cases pass (4 prior + 1 new).

### Task 2: Replace the `Sink` forward-decl stub with the real interface

**Files:** `include/mora/ext/sink.h`

- [ ] **Step 2.1: Overwrite `include/mora/ext/sink.h`**

```cpp
#pragma once

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace mora {
class FactDB;
}

namespace mora::ext {

// Runtime context handed to Sink::emit. Caller configures `config` from
// a CLI flag like `--sink parquet.snapshot=./out` — the sink receives
// the right-hand side ("./out") as `config`. Sinks parse the string as
// they see fit.
struct EmitCtx {
    StringPool& pool;
    DiagBag&    diags;

    // Per-invocation config string (from `--sink <name>=<config>`).
    std::string config;
};

// A Sink consumes a FactDB after evaluation and writes its content
// somewhere. Sinks are registered with the ExtensionContext at
// extension registration time and invoked by the CLI (or other
// drivers) after evaluation.
class Sink {
public:
    virtual ~Sink() = default;

    // Stable identifier (e.g. "parquet.snapshot"). Matches the name
    // used on the CLI's --sink flag.
    virtual std::string_view name() const = 0;

    // Do the work: write `db` out. Errors report through ctx.diags;
    // returning doesn't imply success — check diags.
    virtual void emit(EmitCtx& ctx, const FactDB& db) = 0;
};

} // namespace mora::ext
```

- [ ] **Step 2.2: Quick syntax check**

```bash
cd /home/tbaldrid/oss/mora
clang++ -std=c++20 -Iinclude -fsyntax-only include/mora/ext/sink.h && echo ok
```

Expected: `ok`.

### Task 3: Add `register_sink` + `sinks()` to `ExtensionContext`

**Files:** `include/mora/ext/extension.h`, `src/ext/extension.cpp`

- [ ] **Step 3.1: Edit `include/mora/ext/extension.h`**

Add `#include "mora/ext/sink.h"` at the top (after the existing `data_source.h` include). Then extend the public API:

```cpp
class ExtensionContext {
public:
    ExtensionContext();
    ~ExtensionContext();

    ExtensionContext(const ExtensionContext&) = delete;
    ExtensionContext& operator=(const ExtensionContext&) = delete;

    // Register a DataSource. Takes ownership.
    void register_data_source(std::unique_ptr<DataSource> src);

    // Register a Sink. Takes ownership.
    void register_sink(std::unique_ptr<Sink> sink);

    // Read-only view of all registered data sources, in registration order.
    std::span<const std::unique_ptr<DataSource>> data_sources() const;

    // Read-only view of all registered sinks, in registration order.
    std::span<const std::unique_ptr<Sink>> sinks() const;

    // Convenience driver for data sources (unchanged).
    std::size_t load_required(LoadCtx& ctx, FactDB& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

- [ ] **Step 3.2: Edit `src/ext/extension.cpp`**

Extend `Impl`:

```cpp
struct ExtensionContext::Impl {
    std::vector<std::unique_ptr<DataSource>> sources;
    std::vector<std::unique_ptr<Sink>>       sinks;
};
```

Add the two new method implementations next to `register_data_source` and `data_sources`:

```cpp
void ExtensionContext::register_sink(std::unique_ptr<Sink> sink) {
    impl_->sinks.push_back(std::move(sink));
}

std::span<const std::unique_ptr<Sink>>
ExtensionContext::sinks() const {
    return impl_->sinks;
}
```

- [ ] **Step 3.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: clean build.

### Task 4: Unit-test sink registration

**Files:** `tests/ext/test_extension.cpp`

- [ ] **Step 4.1: Add a StubSink class and two test cases**

Append to `tests/ext/test_extension.cpp`, inside the anonymous namespace (before the closing `} // namespace`):

```cpp
class StubSink : public mora::ext::Sink {
public:
    StubSink(std::string_view name, std::size_t* invocation_counter)
        : name_(name), counter_(invocation_counter) {}

    std::string_view name() const override { return name_; }

    void emit(mora::ext::EmitCtx&, const mora::FactDB&) override {
        ++*counter_;
    }

private:
    std::string  name_;
    std::size_t* counter_;
};

TEST(ExtensionContext, SinkRegistrationPreservesOrder) {
    mora::ext::ExtensionContext ec;
    std::size_t counter = 0;
    ec.register_sink(std::make_unique<StubSink>("a", &counter));
    ec.register_sink(std::make_unique<StubSink>("b", &counter));

    auto sinks = ec.sinks();
    ASSERT_EQ(sinks.size(), 2U);
    EXPECT_EQ(sinks[0]->name(), "a");
    EXPECT_EQ(sinks[1]->name(), "b");
}

TEST(ExtensionContext, SinksAccessorReturnsRegisteredSinks) {
    mora::ext::ExtensionContext ec;
    EXPECT_TRUE(ec.sinks().empty());

    std::size_t counter = 0;
    ec.register_sink(std::make_unique<StubSink>("parquet.snapshot", &counter));

    auto sinks = ec.sinks();
    ASSERT_EQ(sinks.size(), 1U);
    EXPECT_EQ(sinks[0]->name(), "parquet.snapshot");
}
```

- [ ] **Step 4.2: Build + run the test binary**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_extension 2>&1 | tail -3
./build/linux/x86_64/debug/test_extension 2>&1 | tail -15
```

Expected: 7 test cases pass (4 pre-existing from M2 + 1 from Task 1.2 + 2 new).

- [ ] **Step 4.3: Full test suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 85`. (Still 85 binaries; the test_extension binary gained cases but the binary count is unchanged.)

### Task 5: Commit M1

- [ ] **Step 5.1: Stage + commit**

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: Sink API + load_required error-sensitivity fix

Pre-req fix:
  * ExtensionContext::load_required() now snapshots the DiagBag's
    error count before the collision detection pass and bails only
    on NEW errors it emitted itself. Previous behavior bailed on
    any pre-existing errors from earlier pipeline stages — a latent
    bug flagged by the Plan 2 code review.

New API:
  * mora::ext::Sink — virtual interface with name() + emit(ctx, db).
  * mora::ext::EmitCtx — runtime bundle of pool, diags, and a
    per-invocation config string (right-hand side of a CLI flag
    like `--sink parquet.snapshot=./out`).
  * mora::ext::ExtensionContext — gains register_sink() and sinks()
    accessor; storage mirrors the data-source machinery.

Tests:
  * tests/ext/test_extension.cpp adds 3 new gtest cases:
    LoadRequiredToleratesPreexistingErrorsInDiagBag,
    SinkRegistrationPreservesOrder, SinksAccessorReturnsRegisteredSinks.
  * The existing DuplicateProvidesEmitsDiagnostic case is renamed
    to DuplicateProvidesEmitsDiagnostic_GivenEmptyBag for clarity.

No caller of the new Sink API yet — the parquet extension implements
the first real sink in milestone 2.

Part 3 of the v3 rewrite (milestone 1 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5.2: Verify**

```bash
git log -1 --stat
```

Expected: the commit lists `src/ext/extension.cpp`, `include/mora/ext/extension.h`, `include/mora/ext/sink.h`, `tests/ext/test_extension.cpp`.

---

## Milestone 2 — `mora_parquet` extension + `ParquetSnapshotSink`

Goal: pull in the Arrow/parquet C++ library; implement a sink that walks every relation in a `FactDB` and writes one parquet file per relation; roundtrip-test it.

### Task 6: Add the Arrow dep + expose a FactDB enumeration accessor

**Files:** `xmake.lua`, `extensions/parquet/xmake.lua`, `include/mora/eval/fact_db.h`, `src/eval/fact_db.cpp`

- [ ] **Step 6.1: Add `arrow` to xmake requires**

Open `xmake.lua` at the repo root. Find the existing `add_requires("nlohmann_json")` line (around line 140). Below it, add:

```lua
add_requires("arrow 7.0.0", {
    configs = { parquet = true, compute = false, csv = false, jemalloc = false, mimalloc = false }
})
```

(Arrow's default config pulls in a lot of optional subsystems. Disable the ones we don't need to keep build time sane. Parquet support must be explicitly enabled.)

- [ ] **Step 6.2: Update `extensions/parquet/xmake.lua`**

Overwrite with:

```lua
target("mora_parquet")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_includedirs("../../include", {public = false})
    add_files("src/register.cpp", "src/snapshot_sink.cpp")
    add_packages("arrow", {public = false})
    if is_plat("windows") then
        add_packages("fmt", "nlohmann_json", {public = true})
    else
        add_packages("fmt", "nlohmann_json", {public = true})
    end
target_end()

-- Tests for the parquet extension. Mirrors the root xmake's test
-- discovery pattern.
if not is_plat("windows") then
    for _, testfile in ipairs(os.files("tests/test_*.cpp")) do
        local name = path.basename(testfile)
        target(name)
            set_kind("binary")
            set_default(false)
            add_files(testfile)
            add_includedirs("tests")
            add_deps("mora_parquet", "mora_lib")
            add_packages("gtest", "arrow")
            add_syslinks("gtest_main")
            add_tests(name)
        target_end()
    end
end
```

- [ ] **Step 6.3: Add `FactDB::all_relation_names()`**

Edit `include/mora/eval/fact_db.h`. Inside the `FactDB` class, add the declaration right after `size_t fact_count() const;`:

```cpp
    // Returns the interned names of every relation that has at least
    // one configured relation slot (whether populated or empty). Used
    // by sinks to enumerate what to write.
    std::vector<StringId> all_relation_names() const;
```

- [ ] **Step 6.4: Implement `FactDB::all_relation_names` in `src/eval/fact_db.cpp`**

Append a new method definition to the file:

```cpp
std::vector<StringId> FactDB::all_relation_names() const {
    std::vector<StringId> names;
    names.reserve(relations_.size());
    for (const auto& [idx, _rel] : relations_) {
        names.push_back(StringId{idx});
    }
    return names;
}
```

- [ ] **Step 6.5: Configure + fetch**

```bash
cd /home/tbaldrid/oss/mora
xmake f -p linux -m debug --yes 2>&1 | tail -20
```

Expected: xmake resolves + fetches `arrow` (first time: downloads + builds; may take several minutes). If arrow fails to build, stop and escalate — this is an infrastructure concern, not an implementation bug.

- [ ] **Step 6.6: Smoke-build**

```bash
xmake build mora_parquet 2>&1 | tail -10
```

At this point `register.cpp` still references a non-existent `snapshot_sink.cpp` and the build will fail. That's expected — Task 7 creates those files. The goal of 6.6 is only to confirm `arrow` resolves and `mora_lib` picks up `all_relation_names`.

Actually: skip Step 6.6 (it will fail until Task 7 + 8). After Task 8 we'll do the full build.

### Task 7: Declare `ParquetSnapshotSink`

**Files:** `extensions/parquet/include/mora_parquet/snapshot_sink.h`

- [ ] **Step 7.1: Create `extensions/parquet/include/mora_parquet/snapshot_sink.h`**

```cpp
#pragma once

#include "mora/ext/sink.h"

namespace mora_parquet {

// Sink that writes every relation in a FactDB to a parquet file tree.
//
// Layout: for a relation named "form/npc", the file is written to
//   <config>/form/npc.parquet
// For a relation named "plugin_exists", the file is
//   <config>/plugin_exists.parquet
// Intermediate directories are created as needed.
//
// Per-column type inference: each column's Arrow type is inferred from
// the first tuple's Value::Kind at that position. A later tuple whose
// kind differs triggers a diagnostic and the relation is skipped.
//
// List and Var values are not supported in v1. Relations containing
// either are skipped with an info-level diagnostic.
class ParquetSnapshotSink : public mora::ext::Sink {
public:
    std::string_view name() const override;
    void             emit(mora::ext::EmitCtx& ctx,
                           const mora::FactDB& db) override;
};

} // namespace mora_parquet
```

### Task 8: Implement `ParquetSnapshotSink::emit`

**Files:** `extensions/parquet/src/snapshot_sink.cpp`

This is the bulk of M2. The implementation walks every relation name from the FactDB, infers column types, builds Arrow arrays, writes parquet.

- [ ] **Step 8.1: Create `extensions/parquet/src/snapshot_sink.cpp`**

```cpp
#include "mora_parquet/snapshot_sink.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/fact_db.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mora_parquet {

namespace {

namespace fs = std::filesystem;

// Translate one mora::Value::Kind into an Arrow type. Returns nullptr
// for List / Var, which callers interpret as "skip this relation".
std::shared_ptr<arrow::DataType> arrow_type_for(mora::Value::Kind k) {
    switch (k) {
        case mora::Value::Kind::FormID: return arrow::uint32();
        case mora::Value::Kind::Int:    return arrow::int64();
        case mora::Value::Kind::Float:  return arrow::float64();
        case mora::Value::Kind::String: return arrow::utf8();
        case mora::Value::Kind::Bool:   return arrow::boolean();
        case mora::Value::Kind::Var:
        case mora::Value::Kind::List:   return nullptr;
    }
    return nullptr;
}

// Build an Arrow array for a single column. Precondition: every tuple
// has at least `col + 1` values; every value at position `col` has kind
// equal to `kind`. Caller enforces both invariants before calling.
arrow::Result<std::shared_ptr<arrow::Array>>
build_column(const std::vector<mora::Tuple>& tuples,
             std::size_t col, mora::Value::Kind kind,
             const mora::StringPool& pool) {
    using K = mora::Value::Kind;
    switch (kind) {
        case K::FormID: {
            arrow::UInt32Builder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_formid());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Int: {
            arrow::Int64Builder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_int());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Float: {
            arrow::DoubleBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_float());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::String: {
            arrow::StringBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) {
                auto sv = pool.get(t[col].as_string());
                ARROW_RETURN_NOT_OK(b.Append(sv.data(), sv.size()));
            }
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Bool: {
            arrow::BooleanBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_bool());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Var:
        case K::List:
            return arrow::Status::NotImplemented("unsupported kind for parquet emit");
    }
    return arrow::Status::UnknownError("unreachable");
}

// Translate a relation name like "form/npc" into a filesystem-safe
// relative path like "form/npc.parquet". Namespace separators in mora
// relation names are always '/', matching filesystem separators.
fs::path parquet_path_for(std::string_view rel_name, const fs::path& root) {
    return root / (std::string(rel_name) + ".parquet");
}

} // namespace

std::string_view ParquetSnapshotSink::name() const {
    return "parquet.snapshot";
}

void ParquetSnapshotSink::emit(mora::ext::EmitCtx& ctx,
                                const mora::FactDB& db) {
    if (ctx.config.empty()) {
        ctx.diags.error("parquet-config",
            "parquet.snapshot requires a config path: "
            "--sink parquet.snapshot=<out-dir>",
            mora::SourceSpan{}, "");
        return;
    }

    fs::path root(ctx.config);
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        ctx.diags.error("parquet-mkdir",
            fmt::format("parquet.snapshot: cannot create output dir {}: {}",
                        root.string(), ec.message()),
            mora::SourceSpan{}, "");
        return;
    }

    for (auto rel_id : db.all_relation_names()) {
        const auto rel_name = ctx.pool.get(rel_id);
        const auto& tuples = db.get_relation(rel_id);
        if (tuples.empty()) continue;

        const std::size_t arity = tuples.front().size();
        std::vector<mora::Value::Kind> kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            kinds[c] = tuples.front()[c].kind();
        }

        // Skip relations whose first row contains List / Var or whose
        // later rows disagree on column kind.
        bool skip_unsupported = false;
        for (std::size_t c = 0; c < arity; ++c) {
            if (arrow_type_for(kinds[c]) == nullptr) {
                skip_unsupported = true;
                break;
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

        auto schema = arrow::schema(fields);
        auto table  = arrow::Table::Make(schema, columns);

        auto out_path = parquet_path_for(rel_name, root);
        std::error_code mk_ec;
        fs::create_directories(out_path.parent_path(), mk_ec);
        if (mk_ec) {
            ctx.diags.error("parquet-mkdir",
                fmt::format("parquet.snapshot: cannot create dir {}: {}",
                            out_path.parent_path().string(), mk_ec.message()),
                mora::SourceSpan{}, "");
            continue;
        }

        auto outfile = arrow::io::FileOutputStream::Open(out_path.string());
        if (!outfile.ok()) {
            ctx.diags.error("parquet-open",
                fmt::format("parquet.snapshot: cannot open {} for writing: {}",
                            out_path.string(), outfile.status().ToString()),
                mora::SourceSpan{}, "");
            continue;
        }

        // Default writer properties; no compression, no dictionary encoding
        // tweaks. Plan 3 gets correctness; performance tuning lands later.
        auto write_status = parquet::arrow::WriteTable(
            *table, arrow::default_memory_pool(), *outfile,
            /*chunk_size*/ 64 * 1024);
        if (!write_status.ok()) {
            ctx.diags.error("parquet-write",
                fmt::format("parquet.snapshot: WriteTable failed for {}: {}",
                            out_path.string(), write_status.ToString()),
                mora::SourceSpan{}, "");
            continue;
        }
    }
}

} // namespace mora_parquet
```

Note on includes: `fmt::format` is used. If the extension's xmake target doesn't already pull in `fmt` publicly, the include `<fmt/format.h>` needs to be added at the top. (The current `extensions/parquet/xmake.lua` adds `fmt` as a package — so the include is just `<fmt/format.h>`.)

Add `#include <fmt/format.h>` near the top of the file with the other system includes.

- [ ] **Step 8.2: Update `register.cpp`**

Replace the stub with:

```cpp
#include "mora_parquet/register.h"
#include "mora_parquet/snapshot_sink.h"
#include "mora/ext/extension.h"

#include <memory>

namespace mora_parquet {

void register_parquet(mora::ext::ExtensionContext& ctx) {
    ctx.register_sink(std::make_unique<ParquetSnapshotSink>());
}

} // namespace mora_parquet
```

- [ ] **Step 8.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_parquet 2>&1 | tail -10
```

Expected: clean build. If arrow's `parquet/arrow/writer.h` isn't found, the `arrow` package may have been built without parquet — re-check Task 6.1's config flags.

### Task 9: Roundtrip test — write then read

**Files:** `extensions/parquet/tests/test_snapshot_sink_roundtrip.cpp`

- [ ] **Step 9.1: Create the test**

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
                ("mora-parquet-" + label + "-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

TEST(ParquetSnapshotSink, RoundtripsAFullyTypedRelation) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Relation: form/npc(form_id: FormID, name: String, level: Int)
    auto rel = pool.intern("form/npc");
    db.configure_relation(rel, /*arity*/ 3, /*indexed*/ {0});
    auto alice  = pool.intern("Alice");
    auto bob    = pool.intern("Bob");
    db.add_fact(rel, {
        mora::Value::make_formid(0x0100),
        mora::Value::make_string(alice),
        mora::Value::make_int(5),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0101),
        mora::Value::make_string(bob),
        mora::Value::make_int(12),
    });

    auto out_dir = make_temp_dir("roundtrip");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors())
        << "sink emitted errors: "
        << (diags.all().empty() ? "(none)" : diags.all().front().message);

    auto file_path = out_dir / "form" / "npc.parquet";
    ASSERT_TRUE(fs::exists(file_path)) << file_path.string();

    // Read back with parquet::arrow::OpenFile.
    auto infile_result = arrow::io::ReadableFile::Open(file_path.string());
    ASSERT_TRUE(infile_result.ok()) << infile_result.status().ToString();
    auto infile = *infile_result;

    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto open_status = parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader);
    ASSERT_TRUE(open_status.ok()) << open_status.ToString();

    std::shared_ptr<arrow::Table> table;
    ASSERT_TRUE(reader->ReadTable(&table).ok());

    ASSERT_EQ(table->num_columns(), 3);
    ASSERT_EQ(table->num_rows(), 2);
    EXPECT_EQ(table->schema()->field(0)->name(), "col0");
    EXPECT_EQ(table->schema()->field(0)->type()->id(), arrow::Type::UINT32);
    EXPECT_EQ(table->schema()->field(1)->type()->id(), arrow::Type::STRING);
    EXPECT_EQ(table->schema()->field(2)->type()->id(), arrow::Type::INT64);

    auto form_ids = std::static_pointer_cast<arrow::UInt32Array>(
        table->column(0)->chunk(0));
    EXPECT_EQ(form_ids->Value(0), 0x0100u);
    EXPECT_EQ(form_ids->Value(1), 0x0101u);

    auto names = std::static_pointer_cast<arrow::StringArray>(
        table->column(1)->chunk(0));
    EXPECT_EQ(names->GetString(0), "Alice");
    EXPECT_EQ(names->GetString(1), "Bob");

    auto levels = std::static_pointer_cast<arrow::Int64Array>(
        table->column(2)->chunk(0));
    EXPECT_EQ(levels->Value(0), 5);
    EXPECT_EQ(levels->Value(1), 12);
}

TEST(ParquetSnapshotSink, SkipsHeterogeneousRelation) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("bad/mixed");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {});
    db.add_fact(rel, {mora::Value::make_int(1)});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("oops"))});

    auto out_dir = make_temp_dir("hetero");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    EXPECT_FALSE(diags.has_errors());
    bool saw_warning = false;
    for (const auto& d : diags.all()) {
        if (d.code == "parquet-skip-heterogeneous") saw_warning = true;
    }
    EXPECT_TRUE(saw_warning);
    EXPECT_FALSE(fs::exists(out_dir / "bad" / "mixed.parquet"));
}

} // namespace
```

- [ ] **Step 9.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_snapshot_sink_roundtrip 2>&1 | tail -5
./build/linux/x86_64/debug/test_snapshot_sink_roundtrip 2>&1 | tail -15
```

Expected: both test cases pass.

- [ ] **Step 9.3: Full suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 86` (85 + new test_snapshot_sink_roundtrip binary).

### Task 10: Commit M2

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: parquet extension + ParquetSnapshotSink

Adds a first real Sink: ParquetSnapshotSink writes every non-empty
relation in a FactDB to a parquet file tree rooted at the sink's
config path. For relation "form/npc" the output lands at
<config>/form/npc.parquet; intermediate dirs are created.

Column typing is per-relation, inferred from the first tuple:
  * mora::Value::Kind::FormID → arrow::uint32
  * mora::Value::Kind::Int    → arrow::int64
  * mora::Value::Kind::Float  → arrow::float64
  * mora::Value::Kind::String → arrow::utf8 (resolved via StringPool)
  * mora::Value::Kind::Bool   → arrow::boolean

Relations containing List or Var values are skipped with an info
diagnostic. Relations with heterogeneous arity or per-column kinds
between tuples are also skipped with a warning.

xmake.lua gains an `arrow 7.0.0` requires with parquet enabled.
extensions/parquet/xmake.lua now builds mora_parquet against arrow,
and discovers tests under extensions/parquet/tests/.

FactDB::all_relation_names() added so sinks can enumerate without
reaching into private storage.

Tests:
  * test_snapshot_sink_roundtrip — builds a typed relation, emits
    parquet, reads it back via parquet::arrow::FileReader, asserts
    types + values match.
  * Negative test: heterogeneous-column relation → warning + no file.

No CLI wiring yet; parquet snapshot is still opt-in but has no
invocation path. Milestone 3 wires `--sink parquet.snapshot=<path>`.

Part 3 of the v3 rewrite (milestone 2 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 3 — CLI `--sink` flag + sink-integration test

Goal: wire the parquet sink into `mora compile` via a repeatable `--sink` CLI flag, and add an integration-style test that proves the full pipeline (register → load → evaluate → emit) reaches the sink.

### Task 11: Link `mora_parquet` into the `mora` binary

**Files:** `xmake.lua`

- [ ] **Step 11.1: Edit the `mora` target**

Find the `target("mora")` block (around line 215). Change `add_deps("mora_lib", "mora_skyrim_compile")` to `add_deps("mora_lib", "mora_skyrim_compile", "mora_parquet")`.

- [ ] **Step 11.2: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -5
```

Expected: clean build.

### Task 12: Add `--sink` CLI parsing

**Files:** `src/main.cpp`

The existing CLI uses CLI11. The compile subcommand lives around line 820. We add a `--sink` option that's repeatable and takes `<name>=<config>` pairs. Parse the pairs into a `std::unordered_map<std::string, std::string>` keyed by sink name.

- [ ] **Step 12.1: Add a locals for the flag alongside comp_data_dir / comp_output**

Around where other compile locals are declared (search for `std::string comp_output;`), add:

```cpp
    std::vector<std::string> comp_sinks;
```

- [ ] **Step 12.2: Register the flag on the compile subcommand**

Around where `--data-dir` is registered (search for `c_compile->add_option("--data-dir"`), add:

```cpp
    c_compile->add_option("--sink", comp_sinks,
        "Sink to invoke after evaluation. Repeatable. Format: "
        "<sink-name>=<config-string>, e.g. --sink parquet.snapshot=./out")
        ->expected(0, -1);  // repeatable
```

- [ ] **Step 12.3: Parse the pairs into a map early in `cmd_compile`**

Inside the compile handler (`cmd_compile` or the equivalent function that runs after `app.parse`), right after argument parsing but before the pipeline starts, add:

```cpp
    std::unordered_map<std::string, std::string> sink_configs;
    for (const auto& entry : comp_sinks) {
        auto eq = entry.find('=');
        if (eq == std::string::npos) {
            mora::log::error(
                "  --sink '{}' has no '=' — expected <sink-name>=<config>\n",
                entry);
            return 2;
        }
        auto name   = entry.substr(0, eq);
        auto config = entry.substr(eq + 1);
        sink_configs[name] = config;
    }
```

(`comp_sinks` is the flag-collected vector from 12.1.)

### Task 13: Dispatch configured sinks after evaluation

**Files:** `src/main.cpp`

- [ ] **Step 13.1: Add the register + dispatch**

In `cmd_compile`, find where `register_skyrim(ext_ctx)` is called. On the line below it, add the parquet registration:

```cpp
    mora_parquet::register_parquet(ext_ctx);
```

Also add `#include "mora_parquet/register.h"` near the other extension-include lines at the top of the file.

Then, at the very end of the compile pipeline — after `write_patch_file(...)` succeeds, just before the function returns with success — add:

```cpp
    const auto errors_before_sinks = cr.diags.error_count();
    for (const auto& sink : ext_ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx emit_ctx{cr.pool, cr.diags, it->second};
        sink->emit(emit_ctx, db);
    }
    if (cr.diags.error_count() > errors_before_sinks) {
        // New errors came from sinks. Render + bail. Use the same
        // DiagRenderer pattern the compile handler already uses for
        // sema diagnostics (search `DiagRenderer` elsewhere in main.cpp).
        mora::DiagRenderer const renderer(use_color);
        mora::log::info("\n{}", renderer.render_all(cr.diags));
        return 1;
    }
```

(The existing handler already constructs a `mora::DiagRenderer` for sema output — reuse that pattern; `use_color` is a local in `cmd_compile`. If it isn't in scope at this point, hoist or pass it through.)

Check that `mora_parquet::register_parquet` is declared — in `extensions/parquet/include/mora_parquet/register.h`. The scaffold was generated by Plan 1 M1.

- [ ] **Step 13.2: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -5
```

Expected: clean build.

### Task 14: Sink-integration test

**Files:** `tests/cli/test_cli_parquet_sink.cpp`

Tests that the ExtensionContext in the compile pipeline actually dispatches to the parquet sink when `--sink parquet.snapshot=...` is configured. We do this without shelling out to `mora` — instead we call the same registration + dispatch path the CLI does, and verify the parquet file lands on disk.

- [ ] **Step 14.1: Create the test**

```cpp
#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"
#include "mora/ext/extension.h"
#include "mora_parquet/register.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

TEST(CliParquetSink, DispatchesConfiguredSink) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Populate the DB as if sema + eval already happened.
    auto rel = pool.intern("plugin_exists");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("Skyrim.esm"))});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("Update.esm"))});

    // Simulate what cmd_compile does: register sinks on an
    // ExtensionContext, then dispatch those named by --sink.
    mora::ext::ExtensionContext ctx;
    mora_parquet::register_parquet(ctx);

    auto out_dir = fs::temp_directory_path() /
                   ("mora-cli-sink-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    std::unordered_map<std::string, std::string> sink_configs = {
        {"parquet.snapshot", out_dir.string()},
    };

    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx ectx{pool, diags, it->second};
        sink->emit(ectx, db);
    }

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    auto file = out_dir / "plugin_exists.parquet";
    ASSERT_TRUE(fs::exists(file)) << file.string();

    auto infile = *arrow::io::ReadableFile::Open(file.string());
    std::unique_ptr<parquet::arrow::FileReader> reader;
    ASSERT_TRUE(parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader).ok());
    std::shared_ptr<arrow::Table> table;
    ASSERT_TRUE(reader->ReadTable(&table).ok());
    ASSERT_EQ(table->num_rows(), 2);
    ASSERT_EQ(table->num_columns(), 1);
}

TEST(CliParquetSink, NoSinkConfiguredProducesNoFiles) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("plugin_exists");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("Skyrim.esm"))});

    mora::ext::ExtensionContext ctx;
    mora_parquet::register_parquet(ctx);

    // Empty sink_configs map — no sink is dispatched.
    std::unordered_map<std::string, std::string> sink_configs;

    auto out_dir = fs::temp_directory_path() /
                   ("mora-cli-sink-empty-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx ectx{pool, diags, it->second};
        sink->emit(ectx, db);
    }

    EXPECT_FALSE(diags.has_errors());
    EXPECT_FALSE(fs::exists(out_dir));
}

} // namespace
```

- [ ] **Step 14.2: Ensure the test target gets arrow + mora_parquet**

The root `xmake.lua` discovers `tests/**/test_*.cpp` automatically. But each discovered test binary is linked only against `mora_lib`, not `mora_parquet` or `arrow`. For this specific test we need both.

Open the root `xmake.lua`. Find the `tests/**/test_*.cpp` discovery loop (around line 246). Right after the existing loop, add a second targeted declaration:

```lua
-- Override for tests under tests/cli that need the parquet extension
-- (arrow + mora_parquet). Discovered by the preceding glob; this just
-- adds the extra deps.
for _, testfile in ipairs(os.files("tests/cli/test_cli_parquet_*.cpp")) do
    local name = path.basename(testfile)
    target(name)
        add_deps("mora_parquet")
        add_packages("arrow")
    target_end()
end
```

This relies on xmake's additive-target behavior: re-opening a `target()` block with the same name appends options.

If that proves not to work with this xmake version, fall back to writing a dedicated test-target loop for the parquet tests and excluding them from the general loop via a glob pattern.

- [ ] **Step 14.3: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_cli_parquet_sink 2>&1 | tail -5
./build/linux/x86_64/debug/test_cli_parquet_sink 2>&1 | tail -15
```

Expected: both test cases pass.

- [ ] **Step 14.4: Full suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 87` (86 + new test_cli_parquet_sink).

### Task 15: Manual CLI smoke test

Verify that the actual `mora compile` binary accepts and dispatches the sink.

- [ ] **Step 15.1: Run the compile command**

```bash
cd /tmp && mkdir -p mora-parquet-smoke && cd mora-parquet-smoke
echo "namespace smoke" > empty.mora
/home/tbaldrid/oss/mora/build/linux/x86_64/release/mora compile empty.mora \
    --data-dir /tmp/mora-parquet-smoke \
    --sink parquet.snapshot=./parq
echo "exit: $?"
ls -R parq 2>&1 | head -20
```

Expected: exit 0. `parq/` exists (even if empty, because no relations have facts). No crash; no diagnostic.

### Task 16: Commit M3

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: wire --sink into mora compile

Plumbs the parquet snapshot sink through the compile pipeline:

  * `--sink <name>=<config>` CLI flag (repeatable on compile).
    Parsed into an unordered_map<sink-name, config-string>.
  * cmd_compile registers the parquet extension alongside skyrim:
        mora_parquet::register_parquet(ctx);
  * After write_patch_file() lands mora_patches.bin, the compile
    pipeline iterates ctx.sinks() and invokes each sink whose name
    appears in the configs map. EmitCtx carries the sink's config
    string verbatim.

xmake.lua: `mora` binary now depends on mora_parquet.

The parquet snapshot is opt-in; the default `mora compile` invocation
continues to produce only mora_patches.bin. Users who want parquet
must explicitly pass `--sink parquet.snapshot=<out-dir>`.

Tests:
  * tests/cli/test_cli_parquet_sink.cpp — two gtest cases covering
    the dispatch path (with and without the flag configured). Does
    NOT shell out to the mora binary; instead it mirrors the compile
    pipeline's sink-dispatch code against a programmatically-built
    FactDB.

Smoke-tested: `mora compile empty.mora --data-dir ... --sink parquet.snapshot=./out`
exits 0 with the target directory created.

Part 3 of the v3 rewrite (milestone 3 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Done

After Task 16, Plan 3 has landed. Branch state:
- 9 commits on `mora-v3-foundation` (3 P1 + 3 P2 + 3 P3).
- `xmake build` green.
- 87 test binaries pass.
- `mora compile --sink parquet.snapshot=<path>` dumps FactDB relations to parquet alongside `mora_patches.bin`.
- `src/emit/` + the existing binary patch pipeline are untouched. They get deleted in a later plan (spec step 6) once the evaluator produces output relations.

**Deferred / known limitations (explicit, for future plans):**
- **No end-to-end test driving `mora compile --data-dir <skyrim>` against real ESP data.** Plan 2's reviewer #2 concern — the sink-integration test uses a programmatic FactDB, not ESP loading. Addressing this requires CI infrastructure for a Skyrim fixture or a synthetic ESP writer. Defer.
- **Column names are positional (`col0`, `col1`, …).** Relations don't carry column names at the FactDB level until Plan 5 moves schema registration through `ExtensionContext`. Parquet files produced by this plan are machine-readable but not self-describing.
- **No `--prefer-source` disambiguation yet.** Not needed until multiple DataSources can claim the same relation, which won't happen until a second DataSource extension lands.
- **No `is_output` filtering in the snapshot.** The sink writes everything in the FactDB. Once `is_output` metadata exists on schemas (Plan 5), the sink will filter by it.
- **`mora_lib` retains a stale `zlib` dep** (Plan 2 reviewer minor). Deferred cleanup.

**What's next (Plan 4 — not in this plan):**
- Add `is_output` to RelationSchema.
- Port the Skyrim YAML seed through the ExtensionContext (spec step 5's `register_relation`).
- Begin the evaluator-produces-output-relations work so `src/emit/` can eventually be deleted (spec steps 6 → 11).
