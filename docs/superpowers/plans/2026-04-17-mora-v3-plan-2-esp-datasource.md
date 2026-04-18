# Mora v3 — Plan 2: ESP as a DataSource

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all ESP reading + plugin-facts code out of core into the `skyrim_compile` extension, then introduce the `ExtensionContext` + `DataSource` API and route core's fact-loading pipeline through it. At the end of this plan, core contains zero Skyrim-specific code; the Skyrim extension is a real first-class participant instead of a no-op stub.

**Architecture:** Three milestones land in sequence, each leaving the tree green.

- **M1 — Extract (pure refactor):** move `src/esp/`, `src/data/plugin_facts.cpp`, and their headers into `extensions/skyrim_compile/`; rename include paths; link the extension into the `mora` binary. No behavior change.
- **M2 — Interface (new API):** replace the forward-declaration stubs in `include/mora/ext/extension.h` + `data_source.h` with real definitions. Implement `ExtensionContext` in core (`src/ext/`). Unit-test the registration + iteration contract.
- **M3 — Wire up (architectural):** move the ESP orchestration (load order, override filter, plugin facts, parallel extraction) out of `src/main.cpp` into a `SkyrimEspDataSource` inside `skyrim_compile`. `register_skyrim` registers it. `main.cpp` drives loading via `ExtensionContext`. `cr.modules` → `ExtensionContext` → `DataSource::load` → `FactDB`. Byte-for-byte equivalent patch output on the existing fixtures.

**Tech Stack:** C++20, xmake, Google Test. No new external dependencies.

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md`. Plan 2 covers the spec's "Order of operations" steps 3 and 4.

**Branch:** continue on `mora-v3-foundation` — this plan layers three commits on top of Plan 1.

**Baseline:** 84 tests pass, `xmake build` green, HEAD is `c99962e` ("remove stale README"). All changes in this plan must preserve the 84-test baseline unless they explicitly add new tests (Plan 2 adds ExtensionContext tests in M2).

---

## Notation

- **File moves** use `git mv` so history is preserved.
- **#include path rewrite** is mechanical: old path → new path.
- Every step ends with an exact verification command + expected output.
- Every milestone ends with **one commit** via HEREDOC message.

---

## File Map

### M1 — Extract

**Moves (git mv):**
| From | To |
|---|---|
| `src/esp/esp_reader.cpp` | `extensions/skyrim_compile/src/esp/esp_reader.cpp` |
| `src/esp/load_order.cpp` | `extensions/skyrim_compile/src/esp/load_order.cpp` |
| `src/esp/mmap_file.cpp` | `extensions/skyrim_compile/src/esp/mmap_file.cpp` |
| `src/esp/override_filter.cpp` | `extensions/skyrim_compile/src/esp/override_filter.cpp` |
| `src/esp/plugin_index.cpp` | `extensions/skyrim_compile/src/esp/plugin_index.cpp` |
| `src/esp/record_types.cpp` | `extensions/skyrim_compile/src/esp/record_types.cpp` |
| `src/esp/subrecord_reader.cpp` | `extensions/skyrim_compile/src/esp/subrecord_reader.cpp` |
| `include/mora/esp/*.h` (7 files) | `extensions/skyrim_compile/include/mora_skyrim_compile/esp/*.h` |
| `src/data/plugin_facts.cpp` | `extensions/skyrim_compile/src/plugin_facts.cpp` |
| `include/mora/data/plugin_facts.h` | `extensions/skyrim_compile/include/mora_skyrim_compile/plugin_facts.h` |
| `tests/esp_reader_test.cpp` | `extensions/skyrim_compile/tests/esp_reader_test.cpp` |
| `tests/load_order_test.cpp` | `extensions/skyrim_compile/tests/load_order_test.cpp` |
| `tests/mmap_file_test.cpp` | `extensions/skyrim_compile/tests/mmap_file_test.cpp` |
| `tests/override_filter_test.cpp` | `extensions/skyrim_compile/tests/override_filter_test.cpp` |
| `tests/plugin_facts_test.cpp` | `extensions/skyrim_compile/tests/plugin_facts_test.cpp` |
| `tests/plugin_index_test.cpp` | `extensions/skyrim_compile/tests/plugin_index_test.cpp` |
| `tests/record_types_test.cpp` | `extensions/skyrim_compile/tests/record_types_test.cpp` |
| `tests/subrecord_reader_test.cpp` | `extensions/skyrim_compile/tests/subrecord_reader_test.cpp` |
| `tests/skyrim_fixture.h` | `extensions/skyrim_compile/tests/skyrim_fixture.h` |

**Include rewrites (in every surviving `*.cpp`/`*.h` anywhere in the repo):**
- `#include "mora/esp/...` → `#include "mora_skyrim_compile/esp/...`
- `#include "mora/data/plugin_facts.h"` → `#include "mora_skyrim_compile/plugin_facts.h"`

**Modified:**
- `xmake.lua` — drop `"src/esp/*.cpp"` from `mora_lib`'s `add_files(...)`. Remove `"src/data/*.cpp"` is wrong (core still has chunk_pool / value / columnar_relation / indexed_relation / schema_registry); instead, exclude plugin_facts.cpp specifically. Link `mora_skyrim_compile` into the `mora` binary via `add_deps`.
- `extensions/skyrim_compile/xmake.lua` — add the moved source files; add zlib dep (ESP reader uses zlib for compressed record decompression); add a test target that discovers `tests/*_test.cpp` under the extension.

Namespaces stay `mora::` for all ESP types in M1. Renamespacing to `mora::skyrim::` can happen in a later cleanup.

### M2 — Interface

**New files:**
- `src/ext/extension.cpp` — implementation of `ExtensionContext`.
- `src/ext/data_source_registry.cpp` — internal machinery backing `ExtensionContext::register_data_source` + iteration.
- `tests/ext/extension_test.cpp` — unit tests for registration + provides() intersection.

**Modified (stubs → real definitions):**
- `include/mora/ext/extension.h` — `ExtensionContext` class with `register_data_source(std::unique_ptr<DataSource>)` method. Still forward-declares `RelationSchema` / `Sink` / LSP hooks; those land in later plans.
- `include/mora/ext/data_source.h` — `DataSource` abstract base class + `LoadCtx` struct.

**xmake.lua modification:**
- `mora_lib` — add `src/ext/*.cpp` glob.
- A new `tests/ext/*_test.cpp` test target (or verify that the existing test discovery pattern picks it up — it does: `tests/**/test_*.cpp` is already globbed).

### M3 — Wire up

**New files:**
- `extensions/skyrim_compile/src/esp_data_source.cpp` — `SkyrimEspDataSource : public mora::ext::DataSource`.
- `extensions/skyrim_compile/include/mora_skyrim_compile/esp_data_source.h` — class declaration.

**Modified:**
- `extensions/skyrim_compile/src/register.cpp` — `register_skyrim` constructs + registers a `SkyrimEspDataSource`.
- `src/main.cpp` — remove the direct `EspReader` orchestration (phases 1–7: load order build, parsing, runtime index, override filter, plugin facts, parallel extraction, merge). Replace with: create an `ExtensionContext`, call `register_skyrim(ctx)`, call `ctx.load_required(needed, db)` to drive loading.
- Core gets a new helper `load_required(FactDB&, const std::unordered_set<StringId>& needed, LoadCtx&)` on `ExtensionContext` that iterates registered `DataSource`s and invokes each whose `provides()` intersects `needed`.

---

## Baseline

- [ ] **Step B1: Confirm branch state**

```bash
cd /home/tbaldrid/oss/mora
git status
git log --oneline master..HEAD
```

Expected: clean tree; 3 commits on `mora-v3-foundation` (scaffold, big deletion, remove README).

- [ ] **Step B2: Confirm build + test baseline**

```bash
xmake f -p linux -m debug
xmake build 2>&1 | tail -3
xmake test 2>&1 | tail -3
```

Expected: clean build; `100% tests passed, 0 test(s) failed out of 84`.

---

## Milestone 1 — Extract ESP + plugin_facts into skyrim_compile

Goal: file moves + include rewrites + xmake updates. No behavior change; all 84 tests still pass.

### Task 1: Move source + header trees with `git mv`

- [ ] **Step 1.1: Create destination directories**

```bash
cd /home/tbaldrid/oss/mora
mkdir -p extensions/skyrim_compile/src/esp
mkdir -p extensions/skyrim_compile/include/mora_skyrim_compile/esp
mkdir -p extensions/skyrim_compile/tests
```

- [ ] **Step 1.2: Move ESP sources**

```bash
cd /home/tbaldrid/oss/mora
git mv src/esp/esp_reader.cpp        extensions/skyrim_compile/src/esp/
git mv src/esp/load_order.cpp        extensions/skyrim_compile/src/esp/
git mv src/esp/mmap_file.cpp         extensions/skyrim_compile/src/esp/
git mv src/esp/override_filter.cpp   extensions/skyrim_compile/src/esp/
git mv src/esp/plugin_index.cpp      extensions/skyrim_compile/src/esp/
git mv src/esp/record_types.cpp      extensions/skyrim_compile/src/esp/
git mv src/esp/subrecord_reader.cpp  extensions/skyrim_compile/src/esp/
rmdir src/esp
```

Verify: `ls src/esp 2>&1` → "No such file or directory".

- [ ] **Step 1.3: Move ESP headers**

```bash
cd /home/tbaldrid/oss/mora
git mv include/mora/esp/esp_reader.h       extensions/skyrim_compile/include/mora_skyrim_compile/esp/
git mv include/mora/esp/load_order.h       extensions/skyrim_compile/include/mora_skyrim_compile/esp/
git mv include/mora/esp/mmap_file.h        extensions/skyrim_compile/include/mora_skyrim_compile/esp/
git mv include/mora/esp/override_filter.h  extensions/skyrim_compile/include/mora_skyrim_compile/esp/
git mv include/mora/esp/plugin_index.h     extensions/skyrim_compile/include/mora_skyrim_compile/esp/
git mv include/mora/esp/record_types.h     extensions/skyrim_compile/include/mora_skyrim_compile/esp/
git mv include/mora/esp/subrecord_reader.h extensions/skyrim_compile/include/mora_skyrim_compile/esp/
rmdir include/mora/esp
```

- [ ] **Step 1.4: Move plugin_facts**

```bash
cd /home/tbaldrid/oss/mora
git mv src/data/plugin_facts.cpp          extensions/skyrim_compile/src/
git mv include/mora/data/plugin_facts.h   extensions/skyrim_compile/include/mora_skyrim_compile/
```

- [ ] **Step 1.5: Move tests**

```bash
cd /home/tbaldrid/oss/mora
git mv tests/esp_reader_test.cpp        extensions/skyrim_compile/tests/
git mv tests/load_order_test.cpp        extensions/skyrim_compile/tests/
git mv tests/mmap_file_test.cpp         extensions/skyrim_compile/tests/
git mv tests/override_filter_test.cpp   extensions/skyrim_compile/tests/
git mv tests/plugin_facts_test.cpp      extensions/skyrim_compile/tests/
git mv tests/plugin_index_test.cpp      extensions/skyrim_compile/tests/
git mv tests/record_types_test.cpp      extensions/skyrim_compile/tests/
git mv tests/subrecord_reader_test.cpp  extensions/skyrim_compile/tests/
git mv tests/skyrim_fixture.h           extensions/skyrim_compile/tests/
```

- [ ] **Step 1.6: Confirm the moves**

```bash
cd /home/tbaldrid/oss/mora
ls extensions/skyrim_compile/src/esp/ | wc -l
ls extensions/skyrim_compile/include/mora_skyrim_compile/esp/ | wc -l
ls extensions/skyrim_compile/tests/ | wc -l
ls extensions/skyrim_compile/src/plugin_facts.cpp
ls extensions/skyrim_compile/include/mora_skyrim_compile/plugin_facts.h
```

Expected: 7, 7, 9 (8 tests + skyrim_fixture.h), and the two plugin_facts paths present.

### Task 2: Rewrite `#include` paths across the tree

- [ ] **Step 2.1: Rewrite `mora/esp/` includes**

```bash
cd /home/tbaldrid/oss/mora
grep -rl '#include "mora/esp/' \
    src include tests extensions \
    --include='*.cpp' --include='*.h' \
  | xargs sed -i 's|#include "mora/esp/|#include "mora_skyrim_compile/esp/|g'
```

- [ ] **Step 2.2: Rewrite `mora/data/plugin_facts.h` includes**

```bash
cd /home/tbaldrid/oss/mora
grep -rl '#include "mora/data/plugin_facts.h"' \
    src include tests extensions \
    --include='*.cpp' --include='*.h' \
  | xargs sed -i 's|#include "mora/data/plugin_facts.h"|#include "mora_skyrim_compile/plugin_facts.h"|g'
```

- [ ] **Step 2.3: Verify no stale paths remain**

```bash
cd /home/tbaldrid/oss/mora
grep -rnE 'mora/esp/|mora/data/plugin_facts' src include tests extensions --include='*.cpp' --include='*.h' || echo "no matches"
```

Expected: `no matches`.

### Task 3: Update xmake to relocate the sources

- [ ] **Step 3.1: Drop esp + plugin_facts sources from `mora_lib`**

Edit `xmake.lua`. In the `mora_lib` target's `add_files(...)` block, replace `"src/esp/*.cpp"` with nothing (remove the token and its trailing comma), and replace `"src/data/*.cpp"` with `"src/data/chunk_pool.cpp", "src/data/columnar_relation.cpp", "src/data/indexed_relation.cpp", "src/data/schema_registry.cpp", "src/data/value.cpp"` (drops plugin_facts.cpp; lists the surviving five files explicitly so a future data-tree change doesn't accidentally re-pull it in).

After edit, the `add_files(...)` call reads:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/chunk_pool.cpp", "src/data/columnar_relation.cpp",
              "src/data/indexed_relation.cpp", "src/data/schema_registry.cpp",
              "src/data/value.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

- [ ] **Step 3.2: Link `mora_skyrim_compile` into the `mora` binary**

In `xmake.lua`, in the `mora` target (around line 215), add `add_deps("mora_skyrim_compile")` after the existing `add_deps("mora_lib")`:

```lua
target("mora")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("mora_lib", "mora_skyrim_compile")
    add_includedirs("extern/CLI11/include")
    on_load(function (target)
        import("core.project.project")
        target:add("defines", "MORA_VERSION=\"" .. project.version() .. "\"")
    end)
target_end()
```

- [ ] **Step 3.3: Fill out `extensions/skyrim_compile/xmake.lua`**

Replace the current minimal content with:

```lua
target("mora_skyrim_compile")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_includedirs("../../include", {public = false})
    add_files("src/register.cpp",
              "src/plugin_facts.cpp",
              "src/esp/*.cpp")
    if is_plat("windows") then
        add_deps("zlib")
        add_packages("fmt", "nlohmann_json", {public = true})
    else
        add_packages("zlib", "fmt", "nlohmann_json", {public = true})
    end
target_end()

-- Tests for the skyrim_compile extension. Matches the root xmake.lua's
-- test-discovery pattern so behavior is uniform across core + extensions.
if not is_plat("windows") then
    for _, testfile in ipairs(os.files("tests/*_test.cpp")) do
        local name = path.basename(testfile)
        target(name)
            set_kind("binary")
            set_default(false)
            add_files(testfile)
            add_includedirs("tests")
            add_deps("mora_skyrim_compile", "mora_lib")
            add_packages("gtest")
            add_syslinks("gtest_main")
            add_tests(name)
        target_end()
    end
end
```

- [ ] **Step 3.4: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -10
```

Expected: clean build. Possible failure modes:
- "cannot find `mora_skyrim_compile/esp/...`" → an include somewhere still uses the old path. Go back to Task 2.
- "undefined reference to `mora::EspReader::...`" → `mora_skyrim_compile` isn't linked into `mora`; revisit Step 3.2.

### Task 4: Build + test verification

- [ ] **Step 4.1: Full rebuild**

```bash
cd /home/tbaldrid/oss/mora
xmake clean -a
xmake f -p linux -m debug
xmake build 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 4.2: Full test suite**

```bash
xmake test 2>&1 | tail -5
```

Expected: `100% tests passed, 0 test(s) failed out of 84`. The 9 ESP tests now run as part of the skyrim_compile test binary; they should be discovered automatically by the xmake test loop inside `extensions/skyrim_compile/xmake.lua`.

- [ ] **Step 4.3: Straggler grep**

```bash
cd /home/tbaldrid/oss/mora
git grep -nE '"mora/esp/|"mora/data/plugin_facts' -- ':!docs' ':!*.md' || echo "no matches"
```

Expected: `no matches`.

### Task 5: Commit M1

- [ ] **Step 5.1: Review status**

```bash
cd /home/tbaldrid/oss/mora
git status --short | head -20
git diff --stat master..HEAD | tail -10
```

Expected: large number of renames (git's `R` status entries), a handful of modifications (`xmake.lua`, `extensions/skyrim_compile/xmake.lua`, and every file whose includes got rewritten).

- [ ] **Step 5.2: Commit**

```bash
cd /home/tbaldrid/oss/mora
git add -A
git commit -m "$(cat <<'EOF'
mora v3: move ESP + plugin_facts into skyrim_compile extension

Pure refactor. Moves all Skyrim-specific file readers out of core:

  * src/esp/*                        → extensions/skyrim_compile/src/esp/
  * include/mora/esp/*               → extensions/skyrim_compile/include/mora_skyrim_compile/esp/
  * src/data/plugin_facts.cpp        → extensions/skyrim_compile/src/
  * include/mora/data/plugin_facts.h → extensions/skyrim_compile/include/mora_skyrim_compile/
  * tests/{esp_reader,load_order,mmap_file,override_filter,plugin_facts,
            plugin_index,record_types,subrecord_reader}_test.cpp,
    tests/skyrim_fixture.h           → extensions/skyrim_compile/tests/

Include paths rewritten across the tree:
  * "mora/esp/..." → "mora_skyrim_compile/esp/..."
  * "mora/data/plugin_facts.h" → "mora_skyrim_compile/plugin_facts.h"

xmake.lua:
  * mora_lib: drop src/esp/*.cpp; list src/data/*.cpp files explicitly
    (excludes plugin_facts.cpp)
  * mora binary: add_deps("mora_skyrim_compile")

extensions/skyrim_compile/xmake.lua:
  * actually ingest the moved sources
  * link zlib + fmt + nlohmann_json (ESP reader needs them)
  * test target discovery mirroring the root xmake.lua pattern

Namespaces unchanged (still mora::EspReader etc.) — renamespacing
to mora::skyrim:: can happen as follow-up cleanup.

Build green; 84 tests pass (same as baseline).

Part 2 of the v3 rewrite (milestone 1 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5.3: Verify**

```bash
git log -1 --stat | tail -20
```

Expected: the rename summary, a small handful of modifications in the stat.

---

## Milestone 2 — Define ExtensionContext + DataSource API

Goal: replace the forward-declaration stubs in `include/mora/ext/extension.h` + `data_source.h` with real definitions. Implement `ExtensionContext` in core. Unit-test the registration + iteration contract. No caller uses the API yet — that comes in M3.

### Task 6: Define the `DataSource` interface

**Files:**
- Modify: `include/mora/ext/data_source.h`

- [ ] **Step 6.1: Overwrite `include/mora/ext/data_source.h`**

Replace the current stub with:

```cpp
#pragma once

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace mora {
class FactDB;
}

namespace mora::ext {

// Runtime context handed to DataSource::load. Carries the StringPool,
// diagnostic sink, CLI-supplied paths, and anything else loaders need
// that isn't the FactDB itself.
struct LoadCtx {
    StringPool& pool;
    DiagBag&    diags;

    // --data-dir equivalent: root directory for file-system-backed sources.
    std::filesystem::path data_dir;

    // --plugins-txt equivalent: optional load-order manifest path.
    // Empty when not specified.
    std::filesystem::path plugins_txt;

    // The set of relation names (as interned StringId values) that sema
    // says the program references. Loaders MAY use this to skip work
    // for relations nobody asked for.
    std::unordered_set<uint32_t> needed_relations;
};

// A DataSource produces tuples into a FactDB. Loaders are registered
// with the ExtensionContext at extension registration time and invoked
// by the core loader during program execution.
class DataSource {
public:
    virtual ~DataSource() = default;

    // Stable identifier (e.g. "skyrim.esp"). Used in diagnostics and
    // for --prefer-source disambiguation.
    virtual std::string_view name() const = 0;

    // Which relations this source can fill. Returned as a span of
    // interned StringId values. The core loader intersects this with
    // LoadCtx::needed_relations to decide whether to invoke load().
    virtual std::span<const uint32_t> provides() const = 0;

    // Do the work: load facts into `out`. Errors report through
    // ctx.diags; returning doesn't imply success — check diags.
    virtual void load(LoadCtx& ctx, FactDB& out) = 0;
};

} // namespace mora::ext
```

- [ ] **Step 6.2: Quick syntax check**

```bash
cd /home/tbaldrid/oss/mora
clang++ -std=c++20 -Iinclude -fsyntax-only include/mora/ext/data_source.h
echo "ok"
```

Expected: `ok`.

### Task 7: Define the `ExtensionContext` interface

**Files:**
- Modify: `include/mora/ext/extension.h`

- [ ] **Step 7.1: Overwrite `include/mora/ext/extension.h`**

```cpp
#pragma once

#include "mora/ext/data_source.h"

#include <memory>
#include <span>
#include <vector>

namespace mora {
class FactDB;
}

namespace mora::ext {

// ExtensionContext is the handle an extension receives during
// registration. Extensions call register_* to contribute types,
// relations, data sources, sinks, predicates, and LSP providers.
// In Plan 2 only DataSource registration is implemented; additional
// registration methods land in later plans.
class ExtensionContext {
public:
    ExtensionContext();
    ~ExtensionContext();

    ExtensionContext(const ExtensionContext&) = delete;
    ExtensionContext& operator=(const ExtensionContext&) = delete;

    // Register a DataSource. Takes ownership.
    void register_data_source(std::unique_ptr<DataSource> src);

    // Read-only view of all registered data sources, in registration order.
    std::span<const std::unique_ptr<DataSource>> data_sources() const;

    // Convenience driver: invoke every registered DataSource whose
    // provides() intersects ctx.needed_relations, in registration order.
    // Each source writes into `out`. Returns the number of sources
    // actually invoked.
    std::size_t load_required(LoadCtx& ctx, FactDB& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mora::ext
```

- [ ] **Step 7.2: Quick syntax check**

```bash
cd /home/tbaldrid/oss/mora
clang++ -std=c++20 -Iinclude -fsyntax-only include/mora/ext/extension.h
echo "ok"
```

Expected: `ok`.

### Task 8: Implement `ExtensionContext` in core

**Files:**
- Create: `src/ext/extension.cpp`

- [ ] **Step 8.1: Create `src/ext/` directory**

```bash
cd /home/tbaldrid/oss/mora
mkdir -p src/ext
```

- [ ] **Step 8.2: Write `src/ext/extension.cpp`**

```cpp
#include "mora/ext/extension.h"

#include <algorithm>

namespace mora::ext {

struct ExtensionContext::Impl {
    std::vector<std::unique_ptr<DataSource>> sources;
};

ExtensionContext::ExtensionContext()  : impl_(std::make_unique<Impl>()) {}
ExtensionContext::~ExtensionContext() = default;

void ExtensionContext::register_data_source(std::unique_ptr<DataSource> src) {
    impl_->sources.push_back(std::move(src));
}

std::span<const std::unique_ptr<DataSource>>
ExtensionContext::data_sources() const {
    return impl_->sources;
}

std::size_t ExtensionContext::load_required(LoadCtx& ctx, FactDB& out) const {
    std::size_t invoked = 0;
    for (auto& src : impl_->sources) {
        auto provides = src->provides();
        bool any = std::any_of(provides.begin(), provides.end(),
            [&](uint32_t rel) { return ctx.needed_relations.contains(rel); });
        if (!any) continue;
        src->load(ctx, out);
        ++invoked;
    }
    return invoked;
}

} // namespace mora::ext
```

- [ ] **Step 8.3: Add `src/ext/*.cpp` to mora_lib in xmake.lua**

Edit `xmake.lua`. In the `mora_lib` `add_files(...)` block, add `"src/ext/*.cpp"` to the list. Anywhere in the sequence works; keeping alphabetical order, insert between `"src/eval/*.cpp"` and `"src/emit/*.cpp"`:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/ext/*.cpp",
              "src/emit/*.cpp",
              "src/data/chunk_pool.cpp", "src/data/columnar_relation.cpp",
              "src/data/indexed_relation.cpp", "src/data/schema_registry.cpp",
              "src/data/value.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

- [ ] **Step 8.4: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_lib 2>&1 | tail -5
```

Expected: clean build. `libmora_lib.a` now contains the extension.o object.

### Task 9: Unit-test the ExtensionContext registration + iteration

**Files:**
- Create: `tests/ext/extension_test.cpp`

The plan's test style is gtest, TDD: red-green-refactor. Write the failing test first, then the minimal implementation passes it. Here the implementation already exists (Task 8), so the test's purpose is to lock in the contract and catch regressions.

- [ ] **Step 9.1: Create `tests/ext/` directory**

```bash
cd /home/tbaldrid/oss/mora
mkdir -p tests/ext
```

- [ ] **Step 9.2: Write `tests/ext/extension_test.cpp`**

```cpp
#include "mora/ext/extension.h"
#include "mora/eval/fact_db.h"

#include <gtest/gtest.h>

namespace {

class StubSource : public mora::ext::DataSource {
public:
    StubSource(std::string_view name,
               std::vector<uint32_t> provides,
               std::size_t* invocation_counter)
        : name_(name), provides_(std::move(provides)),
          counter_(invocation_counter) {}

    std::string_view name() const override { return name_; }

    std::span<const uint32_t> provides() const override { return provides_; }

    void load(mora::ext::LoadCtx&, mora::FactDB&) override {
        ++*counter_;
    }

private:
    std::string name_;
    std::vector<uint32_t> provides_;
    std::size_t* counter_;
};

TEST(ExtensionContext, EmptyContextInvokesNothing) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, {}};

    mora::ext::ExtensionContext ec;
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 0U);
    EXPECT_TRUE(ec.data_sources().empty());
}

TEST(ExtensionContext, RegisterPreservesInsertionOrder) {
    mora::ext::ExtensionContext ec;
    std::size_t counter = 0;
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<uint32_t>{1}, &counter));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<uint32_t>{2}, &counter));

    auto sources = ec.data_sources();
    ASSERT_EQ(sources.size(), 2U);
    EXPECT_EQ(sources[0]->name(), "a");
    EXPECT_EQ(sources[1]->name(), "b");
}

TEST(ExtensionContext, LoadRequiredOnlyInvokesMatchingSources) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    mora::ext::ExtensionContext ec;
    std::size_t counter_a = 0, counter_b = 0, counter_c = 0;
    ec.register_data_source(std::make_unique<StubSource>("a",
        std::vector<uint32_t>{10, 20}, &counter_a));
    ec.register_data_source(std::make_unique<StubSource>("b",
        std::vector<uint32_t>{30},     &counter_b));
    ec.register_data_source(std::make_unique<StubSource>("c",
        std::vector<uint32_t>{40, 50}, &counter_c));

    // Ask for relations 20 (matches 'a') and 40 (matches 'c'); no ask
    // for anything 'b' provides.
    mora::ext::LoadCtx ctx{pool, diags, {}, {}, /*needed*/ {20U, 40U}};
    auto invoked = ec.load_required(ctx, db);

    EXPECT_EQ(invoked, 2U);
    EXPECT_EQ(counter_a, 1U);
    EXPECT_EQ(counter_b, 0U);
    EXPECT_EQ(counter_c, 1U);
}

} // namespace
```

- [ ] **Step 9.3: Verify the test is discovered and passes**

```bash
cd /home/tbaldrid/oss/mora
xmake test extension_test 2>&1 | tail -10
```

Expected: `[ PASSED ] 3 tests` (three test cases above).

If discovery doesn't pick it up, the `tests/**/test_*.cpp` glob in root `xmake.lua` line ~246 uses `test_*.cpp` but our filename is `extension_test.cpp`. Rename the file to match the other convention: `tests/ext/test_extension.cpp` would match. Re-check the root xmake test-discovery patterns (there are two: `tests/*_test.cpp` at line 231 and `tests/**/test_*.cpp` at line 246). Use whichever fits; prefer `tests/ext/extension_test.cpp` with the first pattern (note: `tests/*_test.cpp` is NOT recursive, so subdirectory files must use `test_*.cpp`). **Correct filename: `tests/ext/test_extension.cpp`.** Rename if needed:

```bash
[ -f tests/ext/extension_test.cpp ] && mv tests/ext/extension_test.cpp tests/ext/test_extension.cpp
```

Then re-run the test.

- [ ] **Step 9.4: Full test suite**

```bash
xmake test 2>&1 | tail -5
```

Expected: `100% tests passed, 0 test(s) failed out of 87` (84 baseline + 3 new).

### Task 10: Commit M2

- [ ] **Step 10.1: Stage + commit**

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: define ExtensionContext + DataSource API

Replaces the forward-declaration stubs in include/mora/ext/ with real
definitions:

  * mora::ext::DataSource — virtual interface with name(), provides(),
    load(ctx, out).
  * mora::ext::LoadCtx — runtime bundle of pool, diags, data_dir,
    plugins_txt, needed_relations set.
  * mora::ext::ExtensionContext — registry + driver. Owns
    std::unique_ptr<DataSource> instances; load_required() iterates
    registered sources and invokes each whose provides() intersects
    the needed_relations set.

New files:
  * include/mora/ext/{extension,data_source}.h — real definitions
    (were forward-declaration stubs).
  * src/ext/extension.cpp — ExtensionContext implementation.
  * tests/ext/test_extension.cpp — three gtest cases covering empty
    context, registration order, and needed-relations filtering.

xmake.lua: mora_lib picks up src/ext/*.cpp via a new glob.

No caller of the new API yet — the ESP pipeline still lives in main.cpp.
That wiring lands in milestone 3 of this plan.

Part 2 of the v3 rewrite (milestone 2 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Milestone 3 — SkyrimEspDataSource + wire through main.cpp

Goal: move the full ESP orchestration (load order build, parsing, runtime index, override filter, plugin-fact emission, parallel extraction, merge) out of `src/main.cpp` into a `SkyrimEspDataSource` inside `skyrim_compile`. `register_skyrim` registers it. `main.cpp` drives loading through `ExtensionContext::load_required`. After this milestone, `main.cpp` contains zero Skyrim-specific code; the Skyrim extension owns the ESP pipeline end-to-end.

### Task 11: Verify existing integration tests are the equivalence gate

The existing `tests/integration_test.cpp` and `tests/backend_integration_test.cpp` already compile fixture `.mora` files through the full pipeline and assert on the resulting `FactDB` / patch content. If these pass unchanged after M3, behavioral equivalence is established without a separate golden.

- [ ] **Step 11.1: Read both integration tests**

```bash
cd /home/tbaldrid/oss/mora
wc -l tests/integration_test.cpp tests/backend_integration_test.cpp
```

Read both files in full. Identify:
- Which fixture(s) they feed to the compile pipeline.
- What they assert on (fact counts, specific tuple presence, patch bytes, or `mora_patches.bin` structure).

- [ ] **Step 11.2: Record baseline pass**

Before starting M3's code changes, run the two tests and confirm they pass:

```bash
xmake test integration_test 2>&1 | tail -3
xmake test backend_integration_test 2>&1 | tail -3
```

Expected: both pass.

- [ ] **Step 11.3: Declare the gate**

For M3, the verification gate is: `integration_test` and `backend_integration_test` both pass after all M3 changes are in place. If either fails, M3's `SkyrimEspDataSource::load` body doesn't faithfully reproduce the pre-M3 orchestration and must be fixed before committing. No separate golden file is created.

### Task 12: Declare `SkyrimEspDataSource`

**Files:**
- Create: `extensions/skyrim_compile/include/mora_skyrim_compile/esp_data_source.h`

- [ ] **Step 12.1: Write the header**

```cpp
#pragma once

#include "mora/ext/data_source.h"
#include "mora_skyrim_compile/esp/esp_reader.h"

#include <memory>
#include <vector>

namespace mora {
class SchemaRegistry;
}

namespace mora_skyrim_compile {

// Data source that loads facts from Skyrim ESP/ESM/ESL plugin files.
// Orchestrates: build load order (data-dir walk or plugins.txt), mmap
// each plugin, build runtime index map, build override filter, emit
// plugin-level facts, then parallel-extract per-record facts.
//
// provides() enumerates every relation registered in the SchemaRegistry
// whose ESP source spec is non-empty — i.e. every relation that ESP
// extraction can populate.
class SkyrimEspDataSource : public mora::ext::DataSource {
public:
    explicit SkyrimEspDataSource(const mora::SchemaRegistry& schema);
    ~SkyrimEspDataSource() override;

    std::string_view          name()     const override;
    std::span<const uint32_t> provides() const override;
    void                      load(mora::ext::LoadCtx& ctx,
                                    mora::FactDB& out) override;

private:
    const mora::SchemaRegistry& schema_;
    std::vector<uint32_t>       provides_;   // populated in the ctor
};

} // namespace mora_skyrim_compile
```

### Task 13: Implement `SkyrimEspDataSource`

**Files:**
- Create: `extensions/skyrim_compile/src/esp_data_source.cpp`

The `load()` body is a literal move of the ESP orchestration currently in `src/main.cpp`. Before M3 starts modifying `src/main.cpp`, record the exact span so we can move it as one unit.

- [ ] **Step 13.1: Isolate the source block**

Open `src/main.cpp`. The ESP orchestration is the contiguous block starting with the first `LoadOrder` construction (either `LoadOrder::from_directory(...)` or `LoadOrder::from_plugins_txt(...)`) and ending with the `out.phase_done(fmt::format("{} plugins, {} relations → {} facts", ...))` line at the end of the parallel extraction + merge. On the baseline at HEAD `c99962e` this block starts around line 380 and ends at line 513.

Record the exact start line, end line, and enclosing function name (e.g. `load_esp_facts` or inlined inside `run_compile`). You'll need these for Step 13.3's variable-name mapping.

- [ ] **Step 13.2: Write the skeleton of `esp_data_source.cpp`**

```cpp
#include "mora_skyrim_compile/esp_data_source.h"

#include "mora_skyrim_compile/esp/esp_reader.h"
#include "mora_skyrim_compile/esp/load_order.h"
#include "mora_skyrim_compile/esp/mmap_file.h"
#include "mora_skyrim_compile/esp/override_filter.h"
#include "mora_skyrim_compile/esp/plugin_index.h"
#include "mora_skyrim_compile/plugin_facts.h"
#include "mora/data/schema_registry.h"
#include "mora/eval/fact_db.h"

#include <algorithm>
#include <future>
#include <thread>
#include <unordered_map>

namespace mora_skyrim_compile {

SkyrimEspDataSource::SkyrimEspDataSource(const mora::SchemaRegistry& schema)
    : schema_(schema)
{
    // Populate provides_ from schema_. Every relation whose ESP source
    // spec is non-empty is a relation this DataSource can fill.
    //
    // SchemaRegistry's relation-iteration API is what src/data/schema_registry.cpp
    // already uses internally — see line ~42 of that file for the idiom
    // (access the internal relations table + read each schema's
    // `esp_sources` vector + the relation's interned-name id).
    //
    // Copy that iteration pattern here; push interned name ids whose
    // esp_sources vector is non-empty.
    //
    // After accumulation, sort + unique so the span returned by
    // provides() has a predictable shape.
    std::sort(provides_.begin(), provides_.end());
    provides_.erase(std::unique(provides_.begin(), provides_.end()),
                    provides_.end());
}

SkyrimEspDataSource::~SkyrimEspDataSource() = default;

std::string_view SkyrimEspDataSource::name() const {
    return "skyrim.esp";
}

std::span<const uint32_t> SkyrimEspDataSource::provides() const {
    return provides_;
}

void SkyrimEspDataSource::load(mora::ext::LoadCtx& ctx, mora::FactDB& out) {
    // The body below is a move of the pre-M3 orchestration in src/main.cpp
    // (see Step 13.1 for the exact line span). Paste that block here, then
    // apply the variable-name rewrites in Step 13.3.
}

} // namespace mora_skyrim_compile
```

Also add `"src/esp_data_source.cpp"` to the `mora_skyrim_compile` xmake target's `add_files(...)` call (alongside the existing entries added in M1 Task 3.3).

- [ ] **Step 13.3: Paste + rewrite the ESP orchestration**

Copy the block identified in Step 13.1 into `SkyrimEspDataSource::load`'s body between the opening and closing braces. Then apply these rewrites (all mechanical):

| Pre-M3 name (from `main.cpp`) | Post-M3 name (inside `load`) |
|---|---|
| `cr.pool` | `ctx.pool` |
| `cr.diags` | `ctx.diags` |
| `data_dir` (`std::string` param) | `ctx.data_dir.string()` |
| `plugins_txt_path` (if present) | `ctx.plugins_txt.string()` |
| `needed` (local `std::unordered_set<uint32_t>`) | `ctx.needed_relations` |
| `db` (the output `FactDB&`) | `out` |
| `schema` (the `SchemaRegistry&`) | `schema_` (the class member) |
| `editor_id_map` (output `unordered_map`) | local map inside `load`; merged into `out` via `out.set_editor_id_map(...)` if that method exists, else captured as a side-effect concern (see Step 13.4) |
| `evaluator.set_symbol_formid(...)` loop | **remove** — evaluator access isn't part of this DataSource; see Step 13.4 |
| `out.phase_start / out.phase_done` (phase-progress output) | **remove** — `mora::Output` isn't part of `LoadCtx`. Progress messaging moves to `main.cpp` outside the `DataSource::load` boundary. |

Everything else (Phase 1 parse futures, Phase 2 override filter, plugin facts call, Phase 3 extraction futures, merge loop) moves verbatim.

- [ ] **Step 13.4: Handle the editor-id-map + evaluator symbol plumbing**

The pre-M3 code has two escapes from the ESP pipeline that aren't strictly part of "load facts into the FactDB":

1. `editor_id_map` (EditorID → FormID) is accumulated during extraction and handed to the evaluator via `evaluator.set_symbol_formid(...)`.
2. The phase-progress output (`out.phase_start` / `out.phase_done`) is a `mora::Output`-side concern.

Neither belongs inside `DataSource::load`'s contract. Two options:

**Option A (recommended):** expose `editor_id_map` through `FactDB` itself. If `FactDB` already has a symbol-registration API (check `include/mora/eval/fact_db.h` for something like `set_symbol_formid` or a symbol-table member), call it from inside `load`. If it doesn't, add a minimal `FactDB::merge_editor_ids(const std::unordered_map<std::string, uint32_t>&)` in this plan — a 3-line addition that doesn't grow the public surface meaningfully.

**Option B:** add an `editor_ids_out` parameter to `LoadCtx` that `load` populates. Caller (`main.cpp`) then feeds it into the evaluator after `load_required` returns.

Pick A if FactDB already has symbol plumbing; pick B otherwise. Document the choice in the M3 commit message.

For phase-progress, move the `out.phase_start("Reading plugins")` call into `src/main.cpp` around the `ctx.load_required(load_ctx, db)` call — the DataSource doesn't emit progress; the driver does.

- [ ] **Step 13.5: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_skyrim_compile 2>&1 | tail -10
```

Expected: clean build. A compile error here usually means a variable-name rewrite got missed; grep the new file for pre-M3 identifiers:

```bash
grep -nE 'cr\.pool|cr\.diags|\bneeded\b' extensions/skyrim_compile/src/esp_data_source.cpp
```

Should return nothing.

- [ ] **Step 13.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_skyrim_compile 2>&1 | tail -10
```

Expected: clean build.

### Task 14: Register the data source

**Files:**
- Modify: `extensions/skyrim_compile/src/register.cpp`

- [ ] **Step 14.1: Rewrite `register.cpp`**

```cpp
#include "mora_skyrim_compile/register.h"
#include "mora_skyrim_compile/esp_data_source.h"
#include "mora/data/schema_registry.h"
#include "mora/ext/extension.h"

#include <memory>

namespace mora_skyrim_compile {

void register_skyrim(mora::ext::ExtensionContext& ctx) {
    // Schema comes from a singleton owned by the extension — for now,
    // a static instance built from the pre-existing YAML-generated
    // relations seed. This tightens up in Plan 5 when relation
    // registration moves to the extension.
    static mora::SchemaRegistry schema;
    static bool initialized = false;
    if (!initialized) {
        schema.register_defaults();
        initialized = true;
    }

    ctx.register_data_source(std::make_unique<SkyrimEspDataSource>(schema));
}

} // namespace mora_skyrim_compile
```

The static-initialization pattern is a temporary bridge; Plan 5 replaces it with proper schema registration through the ExtensionContext.

- [ ] **Step 14.2: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_skyrim_compile 2>&1 | tail -5
```

Expected: clean build.

### Task 15: Rewire `src/main.cpp` to drive loading via `ExtensionContext`

**Files:**
- Modify: `src/main.cpp`

This is the most delicate step. The function `run_compile` (or equivalent in current main.cpp) needs to stop doing ESP work directly.

- [ ] **Step 15.1: Locate the ESP-orchestration block**

In `src/main.cpp`, identify the contiguous block that does ESP loading. It starts around the first use of `LoadOrder::from_directory` / `from_plugins_txt` and ends after the parallel-extraction merge into the primary `FactDB`.

- [ ] **Step 15.2: Replace the block with an ExtensionContext-driven flow**

Structure of the replacement:

```cpp
#include "mora/ext/extension.h"
#include "mora_skyrim_compile/register.h"

// ... inside run_compile or equivalent ...

mora::ext::ExtensionContext ctx;
mora_skyrim_compile::register_skyrim(ctx);

mora::ext::LoadCtx load_ctx{
    cr.pool,
    cr.diags,
    /*data_dir*/   data_dir,
    /*plugins_txt*/ plugins_txt_path,
    /*needed*/     needed_relations,   // already computed from sema
};

ctx.load_required(load_ctx, db);

// After this point `db` is populated; downstream code (evaluation,
// patch emission) is unchanged.
```

All the variables the ESP pipeline used to create (`lo`, `infos`, `runtime_index`, `override_filter`, etc.) are now internal to `SkyrimEspDataSource::load` and no longer exposed to `main.cpp`.

Remove any `#include "mora_skyrim_compile/esp/...` or `#include "mora_skyrim_compile/plugin_facts.h"` from `main.cpp` — main.cpp no longer uses them directly.

- [ ] **Step 15.3: Handle stragglers in main.cpp that still reference moved types**

The pre-M3 `main.cpp` may still reference:
- `mora::EspReader` (deleted use — should be gone)
- `mora::LoadOrder` (in `cmd_info` — keep or remove?)
- `mora::populate_plugin_facts` (deleted use — should be gone)

For each residual reference, decide:
- Is it in the compile pipeline (now owned by SkyrimEspDataSource)? Remove.
- Is it in `cmd_info` (user-facing info command)? The current `cmd_info` reads `LoadOrder::from_directory` to report plugin counts. That's a second, independent ESP access from main.cpp. For Plan 2, the cleanest move is: leave `cmd_info`'s `LoadOrder` usage alone — add an `#include "mora_skyrim_compile/esp/load_order.h"` in `main.cpp`. Full removal of Skyrim-specific code from `main.cpp` can happen in a later plan that introduces an extension API for info-level queries.

Document this choice in the M3 commit message.

- [ ] **Step 15.4: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -10
```

Expected: clean build. `main.cpp` recompiles once; `libmora.a` / `mora` binary relink.

### Task 16: Verification — byte equivalence + full test suite

- [ ] **Step 16.1: Run the full test suite**

```bash
cd /home/tbaldrid/oss/mora
xmake test 2>&1 | tail -5
```

Expected: `100% tests passed, 0 test(s) failed out of 87`.

If the integration tests fail:
- Check `SkyrimEspDataSource::load`'s body against the pre-M3 `src/main.cpp`. The most common cause of drift is missing one of the 7 orchestration phases.
- Re-check the `provides()` computation. If some needed relations don't appear in `provides_`, the core loader will skip invoking the data source for them.

- [ ] **Step 16.2: Golden / fixture byte check (if M3 Task 11 chose the golden-file approach)**

Run the equivalence test added under `extensions/skyrim_compile/tests/test_esp_source_equivalence.cpp`. It must pass.

If M3 Task 11 concluded "existing tests already cover this", the integration test passing in Step 16.1 is the gate.

- [ ] **Step 16.3: Straggler grep**

```bash
cd /home/tbaldrid/oss/mora
grep -nE 'EspReader|populate_plugin_facts|LoadOrder::' src/main.cpp || echo "no compile-pipeline refs"
```

Expected: only the `cmd_info` `LoadOrder::from_directory` reference (documented in Step 15.3). If anything compile-pipeline remains, Step 15.2 missed a move.

### Task 17: Commit M3

- [ ] **Step 17.1: Stage + commit**

```bash
cd /home/tbaldrid/oss/mora
git add -A
git status --short
git commit -m "$(cat <<'EOF'
mora v3: route ESP loading through ExtensionContext + DataSource

Moves the full ESP orchestration out of src/main.cpp into a
SkyrimEspDataSource living inside extensions/skyrim_compile/. The
`mora compile` pipeline now:

  1. Constructs a mora::ext::ExtensionContext.
  2. Calls mora_skyrim_compile::register_skyrim(ctx), which registers
     a SkyrimEspDataSource with the context.
  3. Calls ctx.load_required(load_ctx, db), which iterates registered
     data sources and invokes each whose provides() intersects the
     needed-relations set computed by sema.

main.cpp no longer mentions LoadOrder, EspReader, OverrideFilter,
or populate_plugin_facts for the compile pipeline. The one surviving
Skyrim-specific call is in cmd_info (user-facing `mora info`), which
still uses LoadOrder::from_directory directly; that moves behind an
extension API in a later plan.

SkyrimEspDataSource::load is a verbatim move of the ~100-line
orchestration block from main.cpp; the interface (LoadCtx carries
pool, diags, data_dir, plugins_txt, needed_relations) is designed to
accept the entire original context unmodified. provides() is
computed at construction time by scanning SchemaRegistry for
relations with a non-empty ESP source spec.

register_skyrim uses a function-local static SchemaRegistry for now.
Plan 5 moves schema registration through ExtensionContext.

All 87 tests pass (84 baseline + 3 from M2). Integration tests
confirm the compile pipeline produces functionally-equivalent output
[byte-equivalence check: see <choice documented per Task 11>].

Part 2 of the v3 rewrite (milestone 3 of 3 for this plan).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 17.2: Final diff check**

```bash
cd /home/tbaldrid/oss/mora
git log --oneline master..HEAD
git diff --shortstat master..HEAD
```

Expected: six commits on the branch (three from Plan 1 + three from this plan).

---

## Done

After Task 17, Plan 2 is complete. The branch has 6 commits; each milestone is independently reviewable. The Skyrim extension is no longer a stub — it owns ESP reading and plugin-fact emission, and registers itself through the first real `ExtensionContext` + `DataSource` plumbing.

**What's next (Plan 3 — not in this plan):**

Plan 3 will introduce the `Sink` interface, add the `parquet` extension with `ParquetSnapshotSink`, and delete `src/emit/` entirely. After Plan 3 the compile pipeline is:

```
DataSource::load()  ─►  FactDB(base)
                          ↓
                    Evaluator.evaluate  (unchanged)
                          ↓
                    FactDB(derived)
                          ↓
                    Sink::emit()  ─►  parquet snapshot
```

Plan 3 will scope to spec steps 5 + 6.
