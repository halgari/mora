# Mora v3 — Plan 4: Relation Registration via ExtensionContext

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give extensions a way to register relation schemas with the `ExtensionContext`, add an `is_output` marker to those schemas, and let the parquet sink filter to output-only relations via an `?output-only` config suffix. Introduces the first three Skyrim output relations — `skyrim/set`, `skyrim/add`, `skyrim/remove` — as direct `ctx.register_relation(...)` calls, establishing the pattern for later plans that will delete `src/emit/` and teach the evaluator to produce effect facts.

**Architecture:** Add a second, domain-agnostic `RelationSchema` living under `mora::ext` (distinct from the core `mora::RelationSchema` which is Skyrim-flavored and includes `EspSource` metadata). Extensions register their schemas through a new `ExtensionContext::register_relation()` method; the existing `SchemaRegistry` stays as the canonical source of truth for the Skyrim data path (ESP reader keeps using it directly). `register_skyrim` bridges by iterating the default `SchemaRegistry` and mirroring every relation into the `ExtensionContext`. Parquet sink gains a `?output-only` config knob; when set, it queries `ext::ExtensionContext::schemas()` and filters to relations with `is_output == true`. No evaluator changes — that's a later plan's job.

**Tech Stack:** C++20, xmake, gtest, Apache Arrow.

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md`. Plan 4 delivers a *narrower* slice of spec step 7 — the **architectural surface** (ExtensionContext schema API + `is_output`) and **one concrete use** (parquet filter + three new output relations), deliberately deferring the spec's mechanical bits (YAML re-home, codegen generalization, `src/model/` deletion) to a later plan.

**Branch:** continue on `mora-v3-foundation`. Plan 4 layers three commits on top of Plan 3's nine.

**Baseline:** HEAD `88849c4` (P3 M3). Clean tree. `xmake build` green. 87 test binaries pass.

---

## Design notes (read before editing)

1. **Two `RelationSchema` types, two namespaces.** The existing `mora::RelationSchema` in `include/mora/data/schema_registry.h` stays unchanged — it's Skyrim-flavored (holds `EspSource` vectors, `MoraType` column types bound to a `StringPool`). We add a parallel `mora::ext::RelationSchema` in `include/mora/ext/relation_schema.h` that is pool-agnostic: names are `std::string`, columns carry a name + `indexed` flag, and the schema has an `is_output` boolean and an `std::any ext_metadata`. Conversion is one-way (core → ext) and lossy (we drop `EspSource` / `column_types` for now — the ESP reader and sema keep consuming the core type directly).

2. **`ExtensionContext` gets schema surface.** New methods: `register_relation(RelationSchema)`, `schemas() -> std::span<const RelationSchema>`, `find_schema(std::string_view name) -> const RelationSchema*`.

3. **`EmitCtx` gets a pointer to `ExtensionContext`.** Sinks that need to see registered schemas (parquet's `output-only` filter) walk through the context. Non-nullable in practice; `nullptr` defaults so existing single-sink unit tests still build without plumbing in a context. Forward-declare `ExtensionContext` in `sink.h` to avoid circular includes (`extension.h` already includes `sink.h`).

4. **Skyrim bridge is one-way.** `register_skyrim(ctx)` iterates `SchemaRegistry::all_schemas()` and mirrors each into `ctx`. `SchemaRegistry` remains canonical for the ESP extraction path — we do NOT remove `SchemaRegistry::register_defaults()` or the YAML codegen. Just copy names + per-column name/indexed flags across.

5. **Three new output relations.** `skyrim/set(FormID, String, AnyValue)`, `skyrim/add(FormID, String, AnyValue)`, `skyrim/remove(FormID, String, AnyValue)`. They are NOT added to the YAML/codegen — they live purely as `ctx.register_relation(...)` calls in `register_skyrim`. The evaluator doesn't know about them yet (that's a later plan). Their FactDB slots are empty after Plan 4; parquet's `output-only` filter therefore emits empty parquet files for them, which is the correct v1 behavior (later plans will populate them).

6. **Column-type info is deferred.** Plan 4's `ColumnSpec` intentionally doesn't carry a type — just `name` + `indexed`. Plan 6+ adds type info when the vectorized evaluator lands. The three new output relations declare column names (`entity`, `field`, `value`) but no types.

7. **Parquet filter design.** CLI form: `--sink parquet.snapshot=./out?output-only`. The sink parses its config string: everything before `?` is the output directory, everything after is a `&`-separated list of `k` or `k=v` flags. For Plan 4 we only handle `output-only` (a boolean presence flag). Parser is trivially extensible for later flags like `compression=snappy`.

---

## File Map

### M1 — `ExtensionContext` schema surface

**Modified:**
- `include/mora/ext/relation_schema.h` — upgrade the forward-declaration stub to real definitions of `ColumnSpec` + `RelationSchema`.
- `include/mora/ext/extension.h` — add `register_relation`, `schemas()`, `find_schema()` on `ExtensionContext`.
- `include/mora/ext/sink.h` — forward-declare `ExtensionContext`; extend `EmitCtx` with a nullable `const ExtensionContext* extension = nullptr`.
- `src/ext/extension.cpp` — extend `Impl` with a schemas vector + name→index map; implement the three new methods.
- `tests/ext/test_extension.cpp` — three new gtest cases covering registration, iteration order, lookup by name, and optional-metadata storage.

### M2 — Bridge Skyrim's default schemas into `ExtensionContext`

**Modified:**
- `extensions/skyrim_compile/src/register.cpp` — after constructing the `SkyrimEspDataSource` (unchanged), also build a default `SchemaRegistry`, iterate its schemas, and call `ctx.register_relation(...)` for each.
- `extensions/skyrim_compile/tests/test_register_mirrors_schemas.cpp` (NEW) — verify that `register_skyrim` populates the `ExtensionContext` with the expected Skyrim relation names.

### M3 — Output relations + parquet `?output-only` filter

**Modified:**
- `extensions/skyrim_compile/src/register.cpp` — add three extra `ctx.register_relation(...)` calls for `skyrim/set`, `skyrim/add`, `skyrim/remove` with `is_output = true`.
- `extensions/parquet/src/snapshot_sink.cpp` — parse the config string as `<path>[?<flags>]`, recognize `output-only`; when set, filter relation iteration to those whose schema has `is_output == true`. Relations that are `is_output` but not registered in `ctx.extension->schemas()` (shouldn't happen if callers wire ctx) fall through to the usual path. When `ctx.extension == nullptr` and `output-only` is set, emit a diagnostic error.
- `src/main.cpp` — construct `EmitCtx` with `&ext_ctx` as the extension pointer.
- `tests/cli/test_cli_parquet_sink.cpp` — add a third test case: register the three output relations + one non-output relation, populate all of them, invoke sink with `output-only`, assert only the three output files exist.

---

## Baseline

- [ ] **Step B1: Verify branch state**

```bash
cd /home/tbaldrid/oss/mora
git status
git log master..HEAD --oneline
```

Expected: clean tree; 9 commits on `mora-v3-foundation`; HEAD `88849c4`.

- [ ] **Step B2: Verify build + tests**

```bash
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean; `100% tests passed, 0 test(s) failed out of 87`.

---

## Milestone 1 — `ExtensionContext` schema surface

Goal: real `RelationSchema` + `ColumnSpec` types, `register_relation` / `schemas()` / `find_schema()` on `ExtensionContext`, `EmitCtx` gains an optional context pointer. Three new gtest cases.

### Task 1: Replace the `relation_schema.h` stub

**File:** `include/mora/ext/relation_schema.h` (currently an 11-line forward-decl stub — overwrite entirely)

- [ ] **Step 1.1: Overwrite with real definitions**

```cpp
#pragma once

#include <any>
#include <string>
#include <vector>

namespace mora::ext {

// A relation column description. Pool-agnostic: names are std::string
// and the caller interns them into its own pool at use time. Column
// type info is intentionally absent in Plan 4 — it lands alongside the
// vectorized evaluator work. `indexed` tracks whether the column
// should be hash-indexed for lookup.
struct ColumnSpec {
    std::string name;
    bool        indexed = false;
};

// A relation schema registered by an extension. Pool-agnostic — names
// (both relation and column names) are plain strings.
//
// `is_output` flags relations whose facts represent side effects meant
// to be consumed by downstream stages (currently: parquet sink's
// `output-only` filter). In later plans, the evaluator will produce
// facts into relations with is_output = true and the binary-patch
// machinery under src/emit/ will be retired.
//
// `ext_metadata` is an opaque escape hatch: an extension may attach
// its own state (e.g. EspSource for Skyrim) keyed by a type the
// extension owns. Core code treats it as `std::any` and does not
// interpret it.
struct RelationSchema {
    std::string             name;
    std::vector<ColumnSpec> columns;
    bool                    is_output = false;
    std::any                ext_metadata;
};

} // namespace mora::ext
```

- [ ] **Step 1.2: Syntax check**

```bash
cd /home/tbaldrid/oss/mora
clang++ -std=c++20 -Iinclude -fsyntax-only include/mora/ext/relation_schema.h && echo ok
```

Expected: `ok`.

### Task 2: Extend `ExtensionContext`

**Files:** `include/mora/ext/extension.h`, `src/ext/extension.cpp`

- [ ] **Step 2.1: Edit `include/mora/ext/extension.h`**

Add `#include "mora/ext/relation_schema.h"` near the existing `#include "mora/ext/sink.h"` line (alphabetical). Then extend the public API of `ExtensionContext`, placed right after the `register_sink` method (to keep registration methods grouped):

```cpp
    // Register a RelationSchema. Takes ownership of the copy — caller
    // may let the argument go out of scope after the call. Names must
    // be unique; registering a duplicate name overwrites the prior
    // entry (consistent with how other registry hash-maps in this
    // codebase behave) and is considered a configuration error for
    // callers to avoid.
    void register_relation(RelationSchema schema);

    // Read-only view of all registered relation schemas in registration
    // order. Safe to call after all extensions have registered.
    std::span<const RelationSchema> schemas() const;

    // Look up a schema by name. Returns nullptr if no relation by that
    // name is registered.
    const RelationSchema* find_schema(std::string_view name) const;
```

- [ ] **Step 2.2: Edit `src/ext/extension.cpp`**

Extend the `Impl` struct:

```cpp
struct ExtensionContext::Impl {
    std::vector<std::unique_ptr<DataSource>> sources;
    std::vector<std::unique_ptr<Sink>>       sinks;
    std::vector<RelationSchema>              schemas;
    std::unordered_map<std::string, std::size_t> schema_by_name;
};
```

Add the three new method implementations next to the existing registration methods:

```cpp
void ExtensionContext::register_relation(RelationSchema schema) {
    auto name = schema.name;
    auto& byn = impl_->schema_by_name;
    auto it = byn.find(name);
    if (it != byn.end()) {
        impl_->schemas[it->second] = std::move(schema);
    } else {
        byn[name] = impl_->schemas.size();
        impl_->schemas.push_back(std::move(schema));
    }
}

std::span<const RelationSchema>
ExtensionContext::schemas() const {
    return impl_->schemas;
}

const RelationSchema*
ExtensionContext::find_schema(std::string_view name) const {
    auto it = impl_->schema_by_name.find(std::string(name));
    if (it == impl_->schema_by_name.end()) return nullptr;
    return &impl_->schemas[it->second];
}
```

Ensure `<string>`, `<unordered_map>`, `<vector>` are transitively included — `<unordered_map>` is already included (used for collision detection in `load_required`).

- [ ] **Step 2.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: clean build.

### Task 3: Extend `EmitCtx` with an optional context pointer

**File:** `include/mora/ext/sink.h`

- [ ] **Step 3.1: Forward-declare ExtensionContext + add the field**

Edit `include/mora/ext/sink.h`. Right after `class FactDB;`, add the forward declaration. Then extend `EmitCtx`:

```cpp
namespace mora {
class FactDB;
}

namespace mora::ext {

class ExtensionContext;  // fwd decl — full definition in extension.h

// Runtime context handed to Sink::emit. Caller configures `config` from
// a CLI flag like `--sink parquet.snapshot=./out` — the sink receives
// the right-hand side ("./out") as `config`. Sinks parse the string as
// they see fit.
//
// `extension` is an optional pointer back to the ExtensionContext the
// sink was registered into. Sinks that need to query registered
// RelationSchemas (e.g. to filter by is_output) use this. Callers MUST
// populate it for full functionality; for simple single-sink unit tests
// that don't need schema introspection, leaving it null is fine.
struct EmitCtx {
    StringPool& pool;
    DiagBag&    diags;

    // Per-invocation config string (from `--sink <name>=<config>`).
    std::string config;

    // Optional access to the host ExtensionContext. Populated by the
    // CLI driver before invoking each sink.
    const ExtensionContext* extension = nullptr;
};
```

- [ ] **Step 3.2: Build — confirm no circular-include breakage**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib mora_parquet 2>&1 | tail -5
```

Expected: clean. If the build complains that `ExtensionContext` is incomplete where the sink code tries to DEREFERENCE the pointer, that's a caller-side problem — Plan 4 M3 adds the first deref, not M1.

### Task 4: Unit-test the schema API

**File:** `tests/ext/test_extension.cpp`

- [ ] **Step 4.1: Add three test cases**

Append to `tests/ext/test_extension.cpp` inside the existing anonymous namespace, before the closing `} // namespace`:

```cpp
TEST(ExtensionContext, RegisterRelationPreservesInsertionOrder) {
    mora::ext::ExtensionContext ec;

    mora::ext::RelationSchema a{.name = "a", .columns = {{"col0", false}}};
    mora::ext::RelationSchema b{.name = "b", .columns = {{"col0", true}}};
    ec.register_relation(a);
    ec.register_relation(b);

    auto schemas = ec.schemas();
    ASSERT_EQ(schemas.size(), 2U);
    EXPECT_EQ(schemas[0].name, "a");
    EXPECT_EQ(schemas[1].name, "b");
    EXPECT_FALSE(schemas[0].columns[0].indexed);
    EXPECT_TRUE(schemas[1].columns[0].indexed);
}

TEST(ExtensionContext, FindSchemaByName) {
    mora::ext::ExtensionContext ec;
    ec.register_relation(mora::ext::RelationSchema{
        .name = "form/npc",
        .columns = {{"form_id", true}, {"race", false}},
        .is_output = false,
    });
    ec.register_relation(mora::ext::RelationSchema{
        .name = "skyrim/set",
        .columns = {{"entity", true}, {"field", false}, {"value", false}},
        .is_output = true,
    });

    const auto* npc = ec.find_schema("form/npc");
    ASSERT_NE(npc, nullptr);
    EXPECT_EQ(npc->name, "form/npc");
    EXPECT_FALSE(npc->is_output);

    const auto* setr = ec.find_schema("skyrim/set");
    ASSERT_NE(setr, nullptr);
    EXPECT_TRUE(setr->is_output);

    EXPECT_EQ(ec.find_schema("does/not/exist"), nullptr);
}

TEST(ExtensionContext, RegisterRelationOverwritesDuplicateName) {
    mora::ext::ExtensionContext ec;

    ec.register_relation(mora::ext::RelationSchema{
        .name = "form/npc",
        .columns = {{"col0", false}},
    });
    ec.register_relation(mora::ext::RelationSchema{
        .name = "form/npc",
        .columns = {{"col0", true}, {"col1", false}},
    });

    EXPECT_EQ(ec.schemas().size(), 1U);
    const auto* s = ec.find_schema("form/npc");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->columns.size(), 2U);
    EXPECT_TRUE(s->columns[0].indexed);
}
```

- [ ] **Step 4.2: Build + run the test binary**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_extension 2>&1 | tail -3
./build/linux/x86_64/debug/test_extension 2>&1 | tail -15
```

Expected: 10 gtest cases pass (7 prior + 3 new).

- [ ] **Step 4.3: Full suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 87`.

### Task 5: Commit M1

- [ ] **Step 5.1: Stage + commit**

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: ExtensionContext schema surface

Adds extension-facing relation schemas to mora::ext:

  * mora::ext::RelationSchema — pool-agnostic schema with name,
    columns, is_output flag, and a std::any ext_metadata escape
    hatch. Distinct from the core mora::RelationSchema (which
    stays Skyrim-flavored with EspSource + MoraType vectors).
  * mora::ext::ColumnSpec — column name + indexed flag. Column
    types deferred to a later plan (lands with the vectorized
    evaluator).
  * ExtensionContext::register_relation, schemas(), find_schema()
    — registration by value + two accessors; duplicate names
    overwrite.
  * EmitCtx gains a nullable `const ExtensionContext* extension`
    so sinks can query registered schemas (e.g. filter by
    is_output). ExtensionContext is forward-declared in sink.h
    to avoid a circular include with extension.h.

Tests:
  * tests/ext/test_extension.cpp adds 3 new gtest cases:
    RegisterRelationPreservesInsertionOrder, FindSchemaByName,
    RegisterRelationOverwritesDuplicateName.

No caller of the new API yet — M2 bridges Skyrim's existing
SchemaRegistry into the ExtensionContext; M3 adds the first
is_output relations and wires the parquet sink's filter.

Part 4 of the v3 rewrite (milestone 1 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Also include the untracked plan doc at `docs/superpowers/plans/2026-04-17-mora-v3-plan-4-relation-registration.md` in this commit. `git add -A` will stage it.

- [ ] **Step 5.2: Verify**

```bash
git log -1 --stat
```

Expected: ~6 files — 3 code files + 1 test file + 1 plan doc + potentially updated data_source.h (if the forward-decl reshuffle touches it; most likely only sink.h).

---

## Milestone 2 — Bridge Skyrim's default schemas into `ExtensionContext`

Goal: `register_skyrim` populates `ctx` with every relation that `SchemaRegistry::register_defaults()` produces. No YAML move; `SchemaRegistry` stays canonical for Skyrim's data path. Adds one new test verifying the bridge.

### Task 6: Bridge default schemas inside `register_skyrim`

**File:** `extensions/skyrim_compile/src/register.cpp`

- [ ] **Step 6.1: Edit `register_skyrim`**

Before this milestone the file looks like:

```cpp
#include "mora_skyrim_compile/register.h"
#include "mora_skyrim_compile/esp_data_source.h"
#include "mora/ext/extension.h"

#include <memory>

namespace mora_skyrim_compile {

void register_skyrim(mora::ext::ExtensionContext& ctx) {
    ctx.register_data_source(std::make_unique<SkyrimEspDataSource>());
}

} // namespace mora_skyrim_compile
```

Rewrite to bridge the schemas across:

```cpp
#include "mora_skyrim_compile/register.h"
#include "mora_skyrim_compile/esp_data_source.h"
#include "mora/core/string_pool.h"
#include "mora/data/schema_registry.h"
#include "mora/ext/extension.h"
#include "mora/ext/relation_schema.h"

#include <memory>

namespace mora_skyrim_compile {

namespace {

// Convert the core, StringPool-bound mora::RelationSchema into the
// pool-agnostic mora::ext::RelationSchema that ExtensionContext stores.
// The core schema's EspSource vector and MoraType column types aren't
// carried over — the ESP reader and sema keep consuming the core
// SchemaRegistry directly, so no downstream consumer needs them on the
// ext side yet.
mora::ext::RelationSchema to_ext_schema(const mora::RelationSchema& core,
                                         const mora::StringPool& pool) {
    mora::ext::RelationSchema out;
    out.name = pool.get(core.name);
    out.columns.reserve(core.column_types.size());
    std::unordered_set<size_t> idx(core.indexed_columns.begin(),
                                    core.indexed_columns.end());
    for (size_t i = 0; i < core.column_types.size(); ++i) {
        mora::ext::ColumnSpec c;
        c.name = "col" + std::to_string(i);  // positional; real names land with Plan 5+
        c.indexed = idx.contains(i);
        out.columns.push_back(std::move(c));
    }
    return out;
}

} // namespace

void register_skyrim(mora::ext::ExtensionContext& ctx) {
    ctx.register_data_source(std::make_unique<SkyrimEspDataSource>());

    // Bridge: enumerate the default Skyrim schemas via a throwaway
    // SchemaRegistry bound to a throwaway StringPool, then mirror each
    // into the ExtensionContext so sinks + future consumers can query
    // them pool-agnostically.
    mora::StringPool bridge_pool;
    mora::SchemaRegistry bridge(bridge_pool);
    bridge.register_defaults();
    for (const auto* core : bridge.all_schemas()) {
        ctx.register_relation(to_ext_schema(*core, bridge_pool));
    }
}

} // namespace mora_skyrim_compile
```

Add `#include <unordered_set>` and `#include <string>` at the top if not already transitively included. Check compile errors after the edit — if any header is missing, add it.

- [ ] **Step 6.2: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_skyrim_compile 2>&1 | tail -5
```

Expected: clean build.

### Task 7: Test the bridge

**File:** `extensions/skyrim_compile/tests/test_register_mirrors_schemas.cpp` (NEW)

- [ ] **Step 7.1: Create the test**

```cpp
#include "mora_skyrim_compile/register.h"
#include "mora/ext/extension.h"

#include <gtest/gtest.h>

namespace {

TEST(RegisterSkyrim, MirrorsSchemasIntoExtensionContext) {
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);

    // The default Skyrim schema set is non-trivial; exact count may
    // change as relations are added, but sanity-check it's in the
    // right order of magnitude and that a couple of canonical entries
    // are present.
    auto schemas = ctx.schemas();
    EXPECT_GT(schemas.size(), 10U)
        << "register_skyrim should bridge many relations, got "
        << schemas.size();

    // Spot-check well-known relations the Skyrim defaults always
    // register. If the set of default Skyrim relations ever narrows,
    // update this list — these are checked because they're
    // architectural landmarks.
    EXPECT_NE(ctx.find_schema("form/npc"), nullptr);
    EXPECT_NE(ctx.find_schema("form/weapon"), nullptr);
    EXPECT_NE(ctx.find_schema("plugin_exists"), nullptr);
}

TEST(RegisterSkyrim, MirroredSchemasAreNotOutputs) {
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);

    // None of the default Skyrim input relations should be marked
    // is_output — only the effect relations added in M3 get that flag.
    for (const auto& s : ctx.schemas()) {
        EXPECT_FALSE(s.is_output)
            << "relation '" << s.name << "' unexpectedly marked is_output";
    }
}

} // namespace
```

Note: the second test will start failing at M3 when the three new effect relations are added. That's intentional — M3 updates this test to exclude the three known-output names. For M2 we assert every mirrored schema is non-output.

- [ ] **Step 7.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_register_mirrors_schemas 2>&1 | tail -5
./build/linux/x86_64/debug/test_register_mirrors_schemas 2>&1 | tail -10
```

Expected: 2 test cases pass. If the test target isn't discovered, check `extensions/skyrim_compile/xmake.lua` — its `tests/*_test.cpp` glob should pick up the new file. If the filename format is `test_*.cpp` but the glob uses `*_test.cpp`, rename the file to `register_mirrors_schemas_test.cpp` to match.

- [ ] **Step 7.3: Full suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 88` (87 prior + 1 new binary).

### Task 8: Commit M2

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: bridge Skyrim's default schemas into ExtensionContext

register_skyrim(ctx) now additionally walks a throwaway
SchemaRegistry::register_defaults() and mirrors each relation
into the ExtensionContext via register_relation. Column-type info
and EspSource metadata are dropped on the ext side — the ESP
reader + sema continue consuming the core mora::SchemaRegistry
directly, so no downstream consumer needs them there yet.

The SchemaRegistry stays canonical for Skyrim's data path; this
commit only adds a (lossy) read-only mirror on the ext side so
sinks and future consumers can iterate schemas pool-agnostically.

Tests:
  * extensions/skyrim_compile/tests/test_register_mirrors_schemas.cpp
    (new) — verifies register_skyrim populates ctx with > 10
    relations including form/npc, form/weapon, plugin_exists;
    asserts no mirrored schema is currently flagged is_output
    (M3 will add the first three).

Part 4 of the v3 rewrite (milestone 2 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 3 — Output relations + parquet `?output-only` filter

Goal: three new `is_output = true` relations, parquet sink honors an `?output-only` config suffix, CLI plumbs `ExtensionContext` into each `EmitCtx`. End-to-end integration test.

### Task 9: Register `skyrim/set`, `skyrim/add`, `skyrim/remove`

**File:** `extensions/skyrim_compile/src/register.cpp`

- [ ] **Step 9.1: Add the three output-relation registrations**

Edit `register_skyrim`. After the bridge loop that finishes with `ctx.register_relation(to_ext_schema(*core, bridge_pool));`, add:

```cpp
    // Effect relations — populated by the evaluator in a later plan.
    // For Plan 4 they exist as schemas only; their FactDB slots stay
    // empty, and the parquet sink's output-only filter emits an empty
    // parquet file for each. Once the evaluator starts producing
    // effect facts (spec step 11), these files will carry the data
    // that mora_patches.bin carries today.
    for (std::string_view effect : {"skyrim/set", "skyrim/add", "skyrim/remove"}) {
        ctx.register_relation(mora::ext::RelationSchema{
            .name      = std::string(effect),
            .columns   = {
                {"entity", /*indexed*/ true},
                {"field",  /*indexed*/ false},
                {"value",  /*indexed*/ false},
            },
            .is_output = true,
        });
    }
```

- [ ] **Step 9.2: Update the M2 test to expect exactly three output schemas**

Edit `extensions/skyrim_compile/tests/test_register_mirrors_schemas.cpp`. Replace the `MirroredSchemasAreNotOutputs` case with:

```cpp
TEST(RegisterSkyrim, RegistersExactlyThreeOutputRelations) {
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);

    std::vector<std::string> outputs;
    for (const auto& s : ctx.schemas()) {
        if (s.is_output) outputs.push_back(s.name);
    }
    std::sort(outputs.begin(), outputs.end());

    EXPECT_EQ(outputs,
              (std::vector<std::string>{"skyrim/add", "skyrim/remove", "skyrim/set"}));
}
```

Add `#include <algorithm>` and `#include <vector>` at the top of the test file if needed.

- [ ] **Step 9.3: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_register_mirrors_schemas 2>&1 | tail -3
./build/linux/x86_64/debug/test_register_mirrors_schemas 2>&1 | tail -10
```

Expected: 2 test cases pass (the first — `MirrorsSchemasIntoExtensionContext` — still checks that form/npc etc. are registered; the second now checks the exact three outputs).

### Task 10: Parquet sink honors `?output-only`

**File:** `extensions/parquet/src/snapshot_sink.cpp`

The sink's `emit` currently treats `ctx.config` as a plain filesystem path. Now it becomes `<path>[?<flags>]` with `&`-separated `<k>` or `<k>=<v>` entries.

- [ ] **Step 10.1: Add a config parser**

In the anonymous namespace at the top of `extensions/parquet/src/snapshot_sink.cpp` (alongside `arrow_type_for` and `build_column`), add:

```cpp
// Parsed form of the sink's config string.
struct ParsedConfig {
    fs::path root;
    bool     output_only = false;
};

// Parse `<path>[?<flags>]` where <flags> is k[=v](&k[=v])*. Only recognizes
// the boolean-presence flag `output-only` in Plan 4; unknown flags are
// reported via `unknown_flags` so the caller can diagnose.
ParsedConfig parse_config(std::string_view raw,
                           std::vector<std::string>& unknown_flags) {
    ParsedConfig cfg;
    auto q = raw.find('?');
    cfg.root = fs::path(std::string(raw.substr(0, q)));
    if (q == std::string_view::npos) return cfg;

    std::string_view flags = raw.substr(q + 1);
    while (!flags.empty()) {
        auto amp = flags.find('&');
        std::string_view chunk = flags.substr(0, amp);
        std::string_view key = chunk;
        auto eq = chunk.find('=');
        if (eq != std::string_view::npos) {
            key = chunk.substr(0, eq);
            // value = chunk.substr(eq + 1); — none yet
        }
        if (key == "output-only") {
            cfg.output_only = true;
        } else {
            unknown_flags.emplace_back(key);
        }
        if (amp == std::string_view::npos) break;
        flags = flags.substr(amp + 1);
    }
    return cfg;
}
```

- [ ] **Step 10.2: Route `emit` through the parser + filter**

Find the top of `ParquetSnapshotSink::emit`. The current block:

```cpp
    if (ctx.config.empty()) {
        ctx.diags.error("parquet-config",
            "parquet.snapshot requires a config path: "
            "--sink parquet.snapshot=<out-dir>",
            mora::SourceSpan{}, "");
        return;
    }

    fs::path root(ctx.config);
```

Becomes:

```cpp
    if (ctx.config.empty()) {
        ctx.diags.error("parquet-config",
            "parquet.snapshot requires a config path: "
            "--sink parquet.snapshot=<out-dir>[?output-only]",
            mora::SourceSpan{}, "");
        return;
    }

    std::vector<std::string> unknown_flags;
    ParsedConfig cfg = parse_config(ctx.config, unknown_flags);
    for (const auto& f : unknown_flags) {
        ctx.diags.warning("parquet-unknown-flag",
            fmt::format("parquet.snapshot: unknown flag '{}' ignored", f),
            mora::SourceSpan{}, "");
    }

    // When output-only is requested, we need the ExtensionContext to
    // discover which relations are flagged is_output. Without a context
    // pointer, there's no way to know.
    std::optional<std::unordered_set<std::string>> output_names;
    if (cfg.output_only) {
        if (ctx.extension == nullptr) {
            ctx.diags.error("parquet-output-only",
                "parquet.snapshot: 'output-only' requires an ExtensionContext; "
                "not available in this invocation",
                mora::SourceSpan{}, "");
            return;
        }
        output_names.emplace();
        for (const auto& schema : ctx.extension->schemas()) {
            if (schema.is_output) output_names->insert(schema.name);
        }
    }

    fs::path root = cfg.root;
```

Add `#include <optional>` and `#include <unordered_set>` at the top of the file if not transitively included.

- [ ] **Step 10.3: Apply the filter inside the per-relation loop**

Find the per-relation loop inside `emit`. The top of the loop:

```cpp
    for (auto rel_id : db.all_relation_names()) {
        const auto rel_name = ctx.pool.get(rel_id);
        const auto& tuples = db.get_relation(rel_id);
        if (tuples.empty()) continue;
```

Becomes:

```cpp
    for (auto rel_id : db.all_relation_names()) {
        const auto rel_name = ctx.pool.get(rel_id);
        if (output_names.has_value() &&
            !output_names->contains(std::string(rel_name))) {
            continue;  // filtered out by output-only
        }
        const auto& tuples = db.get_relation(rel_id);
        if (tuples.empty() && !output_names.has_value()) continue;
        // When output-only, emit empty parquet files for declared
        // outputs even if they have no rows yet — downstream tooling
        // can then treat absence as an error rather than "forgot to
        // register".
```

Then, further down, the relation-skipping conditions (`skip_unsupported`, `skip_heterogeneous`) branch on `arity`. If `tuples.empty()`, we didn't compute `arity` — handle that path:

Find the existing:

```cpp
        const std::size_t arity = tuples.front().size();
        std::vector<mora::Value::Kind> kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            kinds[c] = tuples.front()[c].kind();
        }
```

Wrap it:

```cpp
        if (tuples.empty()) {
            // Output-only mode: we were asked to emit even for empty
            // output relations. Look up the schema to get the column
            // count and types. If no schema is registered, skip.
            const auto* schema = ctx.extension->find_schema(std::string(rel_name));
            if (schema == nullptr) {
                ctx.diags.warning("parquet-skip-empty-no-schema",
                    fmt::format("parquet.snapshot: skipping empty relation '{}' "
                                "— no schema registered",
                                rel_name),
                    mora::SourceSpan{}, "");
                continue;
            }
            // Build an empty table with utf8 columns as a placeholder
            // — column types land in Plan 5+; for now the emit is a
            // shape-only zero-row parquet.
            std::vector<std::shared_ptr<arrow::Field>> fields;
            fields.reserve(schema->columns.size());
            for (std::size_t c = 0; c < schema->columns.size(); ++c) {
                fields.push_back(arrow::field(fmt::format("col{}", c),
                                               arrow::utf8()));
            }
            std::vector<std::shared_ptr<arrow::Array>> columns;
            columns.reserve(schema->columns.size());
            for (std::size_t c = 0; c < schema->columns.size(); ++c) {
                arrow::StringBuilder b;
                std::shared_ptr<arrow::Array> out;
                (void)b.Finish(&out);
                columns.push_back(out);
            }
            auto empty_schema = arrow::schema(fields);
            auto empty_table  = arrow::Table::Make(empty_schema, columns);

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
            (void)parquet::arrow::WriteTable(
                *empty_table, arrow::default_memory_pool(), *outfile,
                /*chunk_size*/ 64 * 1024);
            continue;
        }

        const std::size_t arity = tuples.front().size();
        std::vector<mora::Value::Kind> kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            kinds[c] = tuples.front()[c].kind();
        }
```

- [ ] **Step 10.4: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_parquet 2>&1 | tail -5
```

Expected: clean. The existing M2 roundtrip test and M3 sink-integration test should still pass (no `?output-only` suffix → the default path is unchanged).

### Task 11: Plumb `ExtensionContext` into `EmitCtx` in main.cpp

**File:** `src/main.cpp`

- [ ] **Step 11.1: Extend the EmitCtx construction**

Find the sink-dispatch loop inside `cmd_compile`:

```cpp
    for (const auto& sink : ext_ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        const auto errors_before_sink = cr.diags.error_count();
        mora::ext::EmitCtx emit_ctx{cr.pool, cr.diags, it->second};
        sink->emit(emit_ctx, db);
```

Change the EmitCtx construction to include the ExtensionContext pointer:

```cpp
        mora::ext::EmitCtx emit_ctx{cr.pool, cr.diags, it->second, &ext_ctx};
```

- [ ] **Step 11.2: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -3
```

Expected: clean.

### Task 12: End-to-end test — `?output-only` filter

**File:** `tests/cli/test_cli_parquet_sink.cpp`

- [ ] **Step 12.1: Add the new case**

Append, inside the anonymous namespace before `} // namespace`:

```cpp
TEST(CliParquetSink, OutputOnlyFilterEmitsOnlyFlaggedRelations) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Register the real Skyrim bridge + the three is_output relations.
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);
    mora_parquet::register_parquet(ctx);

    // Non-output relation — should NOT be written when output-only.
    auto form_npc = pool.intern("form/npc");
    db.configure_relation(form_npc, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(form_npc, {mora::Value::make_formid(0x0100)});

    // Output relation — should be written, even though empty.
    // (skyrim/set is declared with arity 3 in register_skyrim.)
    // We do NOT populate it; the output-only path emits an empty
    // parquet file for declared outputs.

    auto out_dir = fs::temp_directory_path() /
                   ("mora-output-only-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    // Mirror the dispatch loop from cmd_compile with an output-only flag.
    std::unordered_map<std::string, std::string> sink_configs = {
        {"parquet.snapshot", out_dir.string() + "?output-only"},
    };

    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx emit_ctx{pool, diags, it->second, &ctx};
        sink->emit(emit_ctx, db);
    }

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    // form/npc should NOT be written.
    EXPECT_FALSE(fs::exists(out_dir / "form" / "npc.parquet"));

    // The three is_output relations should all be written (even empty).
    EXPECT_TRUE(fs::exists(out_dir / "skyrim" / "set.parquet"));
    EXPECT_TRUE(fs::exists(out_dir / "skyrim" / "add.parquet"));
    EXPECT_TRUE(fs::exists(out_dir / "skyrim" / "remove.parquet"));
}
```

Also add `#include "mora_skyrim_compile/register.h"` at the top of the test file if not already present. Since the test now needs `mora_skyrim_compile`, the xmake test-discovery override at root `xmake.lua` already ensures this test binary depends on `mora_parquet` + `arrow`; it also needs `mora_skyrim_compile`. Check the override:

```bash
grep -n "test_cli_parquet_" /home/tbaldrid/oss/mora/xmake.lua
```

If the override currently adds only `mora_parquet` and `arrow`, extend it. Find:

```lua
for _, testfile in ipairs(os.files("tests/cli/test_cli_parquet_*.cpp")) do
    local name = path.basename(testfile)
    target(name)
        add_deps("mora_parquet")
        add_packages("arrow")
    target_end()
end
```

and change to:

```lua
for _, testfile in ipairs(os.files("tests/cli/test_cli_parquet_*.cpp")) do
    local name = path.basename(testfile)
    target(name)
        add_deps("mora_parquet", "mora_skyrim_compile")
        add_packages("arrow")
    target_end()
end
```

- [ ] **Step 12.2: Build + run**

```bash
cd /home/tbaldrid/oss/mora
xmake build test_cli_parquet_sink 2>&1 | tail -3
./build/linux/x86_64/debug/test_cli_parquet_sink 2>&1 | tail -15
```

Expected: 3 test cases pass (the 2 prior + 1 new).

- [ ] **Step 12.3: Full suite**

```bash
xmake test 2>&1 | tail -3
```

Expected: `100% tests passed, 0 test(s) failed out of 88` (88 from M2 baseline; no new binary in M3 — we extended an existing one).

### Task 13: Manual CLI smoke test

```bash
cd /tmp && rm -rf mora-p4-smoke && mkdir -p mora-p4-smoke && cd mora-p4-smoke
echo "namespace smoke" > empty.mora
/home/tbaldrid/oss/mora/build/linux/x86_64/release/mora compile empty.mora \
    --data-dir /tmp/mora-p4-smoke \
    --sink "parquet.snapshot=./parq?output-only"
echo "exit=$?"
ls -R parq 2>&1 | head -30
```

Expected: exit 0. `parq/skyrim/set.parquet`, `parq/skyrim/add.parquet`, `parq/skyrim/remove.parquet` exist (empty parquet files). No other files.

If the release build isn't current, rebuild first with `xmake f -p linux -m release --yes && xmake build`.

### Task 14: Commit M3

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: output relations + parquet ?output-only filter

register_skyrim adds three is_output relations directly (not via
YAML codegen):

  * skyrim/set(entity, field, value)     indexed on entity
  * skyrim/add(entity, field, value)     indexed on entity
  * skyrim/remove(entity, field, value)  indexed on entity

The evaluator doesn't yet produce facts into these — that's a
later plan. They exist as schemas today so the parquet sink can
demonstrate the is_output filter end-to-end.

Parquet sink (`extensions/parquet/src/snapshot_sink.cpp`):
  * Config string now parses as `<path>[?<flag>(&<flag>)*]`,
    with `<flag>` = `<k>` | `<k>=<v>`.
  * `output-only` flag filters emission to relations whose
    ExtensionContext-registered schema has is_output = true.
  * When output-only is active, empty output relations get an
    empty parquet file (column count from the registered schema;
    column types default to utf8 — real types land in Plan 5+).
  * Requires `EmitCtx::extension` to be non-null when output-only
    is set; otherwise emits a parquet-output-only error.
  * Unknown flags produce a warning; unrecognized key-value
    pairs pass through for future extensions.

src/main.cpp: cmd_compile's sink-dispatch loop now populates
`emit_ctx.extension = &ext_ctx` so sinks can query registered
schemas.

Tests:
  * tests/cli/test_cli_parquet_sink.cpp adds a third gtest case:
    OutputOnlyFilterEmitsOnlyFlaggedRelations. Registers the
    full Skyrim bridge, populates a non-output relation (form/npc),
    leaves the three is_output relations empty, invokes the sink
    with output-only, asserts the non-output file is absent and
    the three output files exist.
  * test_register_mirrors_schemas.cpp's "no outputs" check is
    replaced with an exact-three-outputs check.
  * Root xmake.lua: test_cli_parquet_* target override now also
    depends on mora_skyrim_compile (needed for register_skyrim).

Part 4 of the v3 rewrite (milestone 3 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Done

After Task 14, Plan 4 is complete. Branch state:
- 12 commits on `mora-v3-foundation` (3 P1 + 3 P2 + 3 P3 + 3 P4).
- `xmake build` green.
- 88 test binaries pass (87 from P3 + 1 new `test_register_mirrors_schemas`).
- `mora compile ... --sink parquet.snapshot=./out?output-only` emits only is_output relations.

**Deferred to later plans:**
- Teach evaluator to produce facts into `skyrim/set`/`add`/`remove`. Once done, the effect relations get real data and `src/emit/` deletion becomes viable.
- Move `data/relations/**.yaml` into `extensions/skyrim_compile/relations/`.
- Generalize `tools/gen_relations.py` and the codegen hook.
- Delete `src/model/relations_seed.cpp`.
- Column types in `mora::ext::ColumnSpec` (needs the vectorized evaluator work).
- Namespace rename for Skyrim-extension types.
- `cmd_info` cleanup.
- `mora_lib`'s stale `zlib` dep.

**What's next (Plan 5 — not in this plan):**
Likely candidate: teach the evaluator about output relations. Concretely, introduce a way for `.mora` rules to derive facts into `skyrim/set(entity, field, value)` (etc.) and have those facts land in the FactDB. This would populate the three is_output relations at eval time; the parquet sink would then produce meaningful data without any further changes. That opens the door to Plan 6 = "delete `src/emit/` + retire `mora_patches.bin`."
