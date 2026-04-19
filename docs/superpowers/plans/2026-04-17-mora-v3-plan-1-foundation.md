# Mora v3 — Plan 1: Foundation (Skeleton + Big Deletion)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lay the groundwork for the Mora v3 rewrite on a new branch. Stand up the extension-point header skeleton and the three extension scaffolds (`parquet`, `skyrim_compile`, `synthetic`) — as no-op targets — then delete the ~3,000 LOC of dynamic-rule / SKSE-runtime / harness / codegen code the new architecture does not need.

**Architecture:** Pure deletion + empty scaffolding. No behavior change for anything that survives. The CLI compile pipeline continues to produce a byte-identical `mora_patches.bin` after milestone 2 (minus the DagBytecode section, which becomes empty when no dynamic rules are compiled).

**Tech Stack:** C++20, xmake build system, Google Test. Python for pre-existing codegen scripts.

**Spec:** `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md` (read for the full target architecture). This plan covers only steps 1–2 of the spec's "Order of operations."

**Branch:** `mora-v3-foundation` off `master`.

---

## File Map

**Milestone 1 — new files:**

- `include/mora/ext/extension.h` — stub: forward-declares `Extension` and `ExtensionContext`.
- `include/mora/ext/nominal_type.h` — stub.
- `include/mora/ext/relation_schema.h` — stub.
- `include/mora/ext/data_source.h` — stub.
- `include/mora/ext/sink.h` — stub.
- `include/mora/ext/predicate.h` — stub.
- `include/mora/ext/lsp_hooks.h` — stub.
- `extensions/parquet/xmake.lua` — defines `mora_parquet` static lib target with one stub source file.
- `extensions/parquet/src/register.cpp` — `register_parquet(ExtensionContext&)` stub that does nothing.
- `extensions/skyrim_compile/xmake.lua` — defines `mora_skyrim_compile` static lib target.
- `extensions/skyrim_compile/src/register.cpp` — `register_skyrim(ExtensionContext&)` stub.
- `extensions/synthetic/xmake.lua` — defines `mora_synthetic` static lib target.
- `extensions/synthetic/src/register.cpp` — `register_synthetic(ExtensionContext&)` stub.

**Milestone 1 — modified files:**

- `xmake.lua` — `includes(...)` the three new extension xmake files.

**Milestone 2 — deletions:**

Source:
- `src/dag/` (3 files: `bytecode.cpp`, `compile.cpp`, `graph.cpp`)
- `src/rt/` (14 files) + `src/rt/handlers/` (7 files)
- `src/harness/` (7 files)
- `src/codegen/` (1 file: `address_library.cpp`; delete directory)

Includes:
- `include/mora/dag/` (5 files)
- `include/mora/rt/` (11 files)
- `include/mora/harness/` (6 files)
- `include/mora/codegen/` (1 file: `address_library.h`; delete directory)

Tests:
- `tests/dag/` (4 files)
- `tests/rt/` (14 files)
- `tests/cli/test_dag_section_emitted.cpp` (the only non-`rt/` test that depends on the dag)

Scripts:
- `scripts/deploy_runtime.sh`
- `scripts/test_runtime.sh`
- `scripts/MoraRuntime.def`

**Milestone 2 — modified files:**

- `src/main.cpp` — drop `#include "mora/dag/compile.h"` and `#include "mora/dag/bytecode.h"` (lines 19–20). Delete the dag-compile block (lines 645–653). Change the `serialize_patch_table` call at line 656–657 to pass an empty `std::vector<uint8_t>{}` for `dag_payload`.
- `xmake.lua` — drop `"src/codegen/*.cpp"`, `"src/rt/*.cpp"`, `"src/rt/handlers/*.cpp"`, `"src/harness/*.cpp"`, `"src/dag/*.cpp"` from the `mora_lib` `add_files(...)` call (lines 160–167). Delete the entire `if is_plat("windows") then ... end` SKSE block (lines 262/282–438) that defines `commonlibsse_ng`, `spdlog_rt`, `mora_runtime`, `mora_test_harness` (keep the earlier Windows-CLI block at lines 83–154 — that's for the CLI, still needed).
- `.github/workflows/ci.yml` — remove the `xmake build -y mora_runtime` and `xmake build -y mora_test_harness` steps (lines ~180–200). Preserve the rest of the CI pipeline.

---

## Baseline (read once)

Check the spec so you know what this is building toward, then check the existing repo shape:

- [ ] **Step B1: Read the spec** — `/home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md`. You don't need to memorize it, but skim the "Order of operations" and "Core data model" sections.

- [ ] **Step B2: Confirm clean working tree on master**

```bash
cd /home/tbaldrid/oss/mora
git status
git log -1 --oneline
```

Expected: working tree clean, on `master`.

- [ ] **Step B3: Record the baseline build + test state**

```bash
cd /home/tbaldrid/oss/mora
xmake f -p linux -m debug
xmake build 2>&1 | tail -20
xmake test 2>&1 | tail -20
```

Expected: build succeeds, all tests green. Note the test count (you'll compare after deletions).

- [ ] **Step B4: Create the feature branch**

```bash
git checkout -b mora-v3-foundation
```

Expected: switched to new branch.

---

## Milestone 1 — Skeleton

Goal: add `include/mora/ext/` header stubs and three empty extension targets. Build stays green and unchanged.

### Task 1: Create the `include/mora/ext/` header stubs

**Files:**
- Create: `include/mora/ext/extension.h`
- Create: `include/mora/ext/nominal_type.h`
- Create: `include/mora/ext/relation_schema.h`
- Create: `include/mora/ext/data_source.h`
- Create: `include/mora/ext/sink.h`
- Create: `include/mora/ext/predicate.h`
- Create: `include/mora/ext/lsp_hooks.h`

- [ ] **Step 1.1: Write `extension.h`**

```cpp
#pragma once

// Foundation stub. Populated in Plan 3 when the ExtensionContext API is
// introduced. For now only forward declarations so extension register()
// functions can compile against a known type name.

namespace mora::ext {

class Extension;
class ExtensionContext;

} // namespace mora::ext
```

- [ ] **Step 1.2: Write `nominal_type.h`**

```cpp
#pragma once

// Foundation stub. The singleton Type class and nominal-type registry
// arrive in the data-model plan. This header exists so extension headers
// that will eventually include it compile as empty today.

namespace mora::ext {

class Type;

} // namespace mora::ext
```

- [ ] **Step 1.3: Write `relation_schema.h`**

```cpp
#pragma once

// Foundation stub. RelationSchema / ColumnSpec arrive alongside the
// columnar FactDB rewrite.

namespace mora::ext {

struct ColumnSpec;
struct RelationSchema;

} // namespace mora::ext
```

- [ ] **Step 1.4: Write `data_source.h`**

```cpp
#pragma once

// Foundation stub. DataSource interface arrives when we split the ESP
// reader out of the core.

namespace mora::ext {

class DataSource;

} // namespace mora::ext
```

- [ ] **Step 1.5: Write `sink.h`**

```cpp
#pragma once

// Foundation stub. Sink interface arrives when the parquet snapshot
// writer replaces src/emit/.

namespace mora::ext {

class Sink;

} // namespace mora::ext
```

- [ ] **Step 1.6: Write `predicate.h`**

```cpp
#pragma once

// Foundation stub. Predicate-registration API arrives with the
// vectorized evaluator.

namespace mora::ext {

class PredicateCtx;

} // namespace mora::ext
```

- [ ] **Step 1.7: Write `lsp_hooks.h`**

```cpp
#pragma once

// Foundation stub. HoverProvider / GotoProvider / CompletionProvider
// arrive when Skyrim-specific LSP behavior moves behind provider
// interfaces.

namespace mora::ext::lsp {

class HoverProvider;
class GotoProvider;
class CompletionProvider;

} // namespace mora::ext::lsp
```

- [ ] **Step 1.8: Verify the headers parse**

```bash
cd /home/tbaldrid/oss/mora
for h in include/mora/ext/*.h; do
  clang++ -std=c++20 -Iinclude -fsyntax-only "$h" || { echo "FAIL: $h"; exit 1; }
done
echo "all ext headers parse"
```

Expected: `all ext headers parse`.

### Task 2: Scaffold the `parquet` extension

**Files:**
- Create: `extensions/parquet/xmake.lua`
- Create: `extensions/parquet/src/register.cpp`

- [ ] **Step 2.1: Write `extensions/parquet/src/register.cpp`**

```cpp
#include "mora/ext/extension.h"

// Foundation stub. Populated when the parquet snapshot sink lands.
namespace mora_parquet {

void register_parquet(mora::ext::ExtensionContext& /*ctx*/) {
    // no-op
}

} // namespace mora_parquet
```

- [ ] **Step 2.2: Write `extensions/parquet/xmake.lua`**

```lua
target("mora_parquet")
    set_kind("static")
    add_includedirs("../../include", {public = false})
    add_files("src/register.cpp")
target_end()
```

### Task 3: Scaffold the `skyrim_compile` extension

**Files:**
- Create: `extensions/skyrim_compile/xmake.lua`
- Create: `extensions/skyrim_compile/src/register.cpp`

- [ ] **Step 3.1: Write `extensions/skyrim_compile/src/register.cpp`**

```cpp
#include "mora/ext/extension.h"

// Foundation stub. Populated as Skyrim-specific code migrates out of
// core (ESP reader, form_model, relations YAML, verbs, LSP enrichment).
namespace mora_skyrim_compile {

void register_skyrim(mora::ext::ExtensionContext& /*ctx*/) {
    // no-op
}

} // namespace mora_skyrim_compile
```

- [ ] **Step 3.2: Write `extensions/skyrim_compile/xmake.lua`**

```lua
target("mora_skyrim_compile")
    set_kind("static")
    add_includedirs("../../include", {public = false})
    add_files("src/register.cpp")
target_end()
```

### Task 4: Scaffold the `synthetic` extension

**Files:**
- Create: `extensions/synthetic/xmake.lua`
- Create: `extensions/synthetic/src/register.cpp`

- [ ] **Step 4.1: Write `extensions/synthetic/src/register.cpp`**

```cpp
#include "mora/ext/extension.h"

// Foundation stub. Will register one nominal type, one relation, and
// one sink — purely to keep the core honest about being domain-free.
namespace mora_synthetic {

void register_synthetic(mora::ext::ExtensionContext& /*ctx*/) {
    // no-op
}

} // namespace mora_synthetic
```

- [ ] **Step 4.2: Write `extensions/synthetic/xmake.lua`**

```lua
target("mora_synthetic")
    set_kind("static")
    add_includedirs("../../include", {public = false})
    add_files("src/register.cpp")
target_end()
```

### Task 5: Wire extension targets into the root xmake.lua

**Files:**
- Modify: `xmake.lua` — add `includes(...)` lines after the `mora_lib` target definition.

- [ ] **Step 5.1: Locate the insertion point**

The `mora_lib` target is defined on lines 157–205 and `mora` on lines 208–222. Insert the `includes()` lines directly after `mora_lib`'s `target_end()` (line 205) and before the `mora` target on line 208.

- [ ] **Step 5.2: Add the extension includes**

Edit `xmake.lua`. Find the line `target_end()` at line 205 (immediately after `mora_lib`'s `before_build(...)` block). After that line, insert:

```lua

-- ══════════════════════════════════════════════════════════════
-- Extensions (foundation stubs — no-op until Plan 3)
-- ══════════════════════════════════════════════════════════════
includes("extensions/parquet/xmake.lua")
includes("extensions/skyrim_compile/xmake.lua")
includes("extensions/synthetic/xmake.lua")
```

The resulting structure is: `mora_lib` target definition ends → extension includes → `mora` binary target.

- [ ] **Step 5.3: Build the three extension targets**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora_parquet mora_skyrim_compile mora_synthetic 2>&1 | tail -20
```

Expected: three `.a` (or `.lib`) static libraries built. Each contains one TU with one no-op function. No warnings on Linux (we build with `/WX`-equivalent).

- [ ] **Step 5.4: Build everything; confirm nothing else changed**

```bash
cd /home/tbaldrid/oss/mora
xmake build 2>&1 | tail -20
xmake test 2>&1 | tail -5
```

Expected: full build green; all tests pass; same test count as baseline (step B3). The new extension targets are built but nothing depends on them yet.

### Task 6: Commit milestone 1

- [ ] **Step 6.1: Stage the new files**

```bash
cd /home/tbaldrid/oss/mora
git add include/mora/ext/ extensions/ xmake.lua
git status
```

Expected: seven new headers under `include/mora/ext/`, six new files under `extensions/`, one modified `xmake.lua`. Nothing else.

- [ ] **Step 6.2: Commit**

```bash
git commit -m "$(cat <<'EOF'
mora v3: scaffold ext/ headers and empty extension targets

Adds include/mora/ext/ header stubs (forward declarations only) and
three no-op static-lib extension targets: mora_parquet,
mora_skyrim_compile, mora_synthetic. All three build but nothing in
the tree depends on them yet; they exist so subsequent plans can
populate them incrementally.

No behavior change. Part of the v3 rewrite (plan 1 of N).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6.3: Verify the commit**

```bash
git log -1 --stat
```

Expected: ~14 files changed, all additions (plus the xmake.lua modification).

---

## Milestone 2 — Big Deletion

Goal: delete the dynamic-rules + SKSE-runtime + harness + codegen subsystems (~3,000 LOC). Build stays green after this milestone; the CLI produces `mora_patches.bin` with an empty DagBytecode section instead of a populated one.

### Task 7: Strip the dag compile from `src/main.cpp`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 7.1: Remove the dag includes**

Open `src/main.cpp` and delete lines 19–20:

```cpp
#include "mora/dag/compile.h"
#include "mora/dag/bytecode.h"
```

- [ ] **Step 7.2: Remove the dag compile block and pass an empty payload**

In `src/main.cpp`, find the block (around lines 645–657):

```cpp
    // Compile dynamic rules to an operator DAG and emit as DagBytecode section.
    mora::dag::DagGraph dag_graph;
    for (const auto& m : modules) {
        mora::dag::compile_dynamic_rules(m, pool, dag_graph);
    }
    std::vector<uint8_t> dag_payload;
    if (dag_graph.node_count() > 0) {
        dag_payload = mora::dag::serialize_dag(dag_graph);
    }

    out.phase_start("Serializing patches");
    auto patch_data = mora::serialize_patch_table(
        patch_buf.entries(), digest, arrangements_section, dag_payload, string_table);
```

Replace with:

```cpp
    out.phase_start("Serializing patches");
    auto patch_data = mora::serialize_patch_table(
        patch_buf.entries(), digest, arrangements_section,
        std::vector<uint8_t>{}, string_table);
```

(The `std::vector<uint8_t>{}` is the now-always-empty `dag_payload` placeholder. The `serialize_patch_table` overload stays intact; we retire it when `src/emit/` is deleted in a later plan.)

- [ ] **Step 7.3: Build and confirm only dag is gone**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora 2>&1 | tail -20
```

Expected: `mora` binary links cleanly. The dag/* sources still compile as part of `mora_lib` (we delete them in the next tasks) — they just aren't called from `main.cpp` anymore.

### Task 8: Delete `src/dag/` + `include/mora/dag/` + `tests/dag/` + `tests/cli/test_dag_section_emitted.cpp`

**Files:**
- Delete directory: `src/dag/`
- Delete directory: `include/mora/dag/`
- Delete directory: `tests/dag/`
- Delete file: `tests/cli/test_dag_section_emitted.cpp`

- [ ] **Step 8.1: Delete the source directories**

```bash
cd /home/tbaldrid/oss/mora
git rm -r src/dag include/mora/dag tests/dag tests/cli/test_dag_section_emitted.cpp
```

Expected: git reports 3 files from src/dag, 5 from include/mora/dag, 4 from tests/dag, 1 from tests/cli — 13 files total.

- [ ] **Step 8.2: Remove the dag glob from xmake `mora_lib`**

Edit `xmake.lua` lines 160–167. Find:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp",
              "src/codegen/*.cpp", "src/rt/*.cpp", "src/rt/handlers/*.cpp",
              "src/harness/*.cpp",
              "src/model/*.cpp", "src/dag/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

Remove `, "src/dag/*.cpp"` from the second-to-last line. Keep `src/codegen/*.cpp`, `src/rt/*.cpp`, `src/rt/handlers/*.cpp`, `src/harness/*.cpp`, `src/model/*.cpp` for now — those get removed in Task 11.

Resulting block:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp",
              "src/codegen/*.cpp", "src/rt/*.cpp", "src/rt/handlers/*.cpp",
              "src/harness/*.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

- [ ] **Step 8.3: Build to confirm dag is gone**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora 2>&1 | tail -10
```

Expected: clean build. The `mora` binary no longer contains any dag code.

### Task 9: Delete `src/rt/` + `include/mora/rt/` + `tests/rt/`

- [ ] **Step 9.1: Delete the files**

```bash
cd /home/tbaldrid/oss/mora
git rm -r src/rt include/mora/rt tests/rt
```

Expected: 21 src files (14 + 7 handlers), 11 include files, 14 test files — 46 files total.

- [ ] **Step 9.2: Remove rt globs from xmake `mora_lib`**

Edit `xmake.lua`. In the `mora_lib` `add_files(...)` block, remove `"src/rt/*.cpp", "src/rt/handlers/*.cpp"`. Resulting block:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp",
              "src/codegen/*.cpp",
              "src/harness/*.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

- [ ] **Step 9.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora 2>&1 | tail -10
```

Expected: clean build. Some other code may include `mora/rt/...` — if so, the compiler reports missing headers; proceed to Task 10 which deletes those consumers (harness), else investigate. Any remaining consumer inside `mora_lib` indicates an unplanned coupling — inspect with `grep -rn 'mora/rt/' src include`.

### Task 10: Delete `src/harness/` + `include/mora/harness/`

- [ ] **Step 10.1: Delete the files**

```bash
cd /home/tbaldrid/oss/mora
git rm -r src/harness include/mora/harness
```

Expected: 7 src files, 6 include files — 13 files total.

- [ ] **Step 10.2: Remove harness glob from xmake `mora_lib`**

Edit `xmake.lua`. Remove `"src/harness/*.cpp"` from the `add_files(...)` block:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp",
              "src/codegen/*.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

- [ ] **Step 10.3: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora 2>&1 | tail -10
```

Expected: clean build.

### Task 11: Delete `src/codegen/address_library.cpp` + `include/mora/codegen/`

**Note:** `src/codegen/` contains only `address_library.cpp` (the LLVM-codegen leftover). The directory is removed entirely.

- [ ] **Step 11.1: Delete the files**

```bash
cd /home/tbaldrid/oss/mora
git rm -r src/codegen include/mora/codegen
```

Expected: 1 src file, 1 include file — 2 files total.

- [ ] **Step 11.2: Remove codegen glob from xmake `mora_lib`**

Edit `xmake.lua`. Remove `"src/codegen/*.cpp",` from the `add_files(...)` block:

```lua
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
```

- [ ] **Step 11.3: Remove the stale `MORA_WITH_COMMONLIB` comment**

Still in `xmake.lua`, delete the now-stale comment inside the `mora_lib` target (was lines 168–170 before edits):

```lua
    -- Runtime files that touch CommonLibSSE-NG guard their bodies on
    -- MORA_WITH_COMMONLIB. The CLI doesn't link CommonLib, so leave
    -- that undefined here — guarded blocks compile as empty stubs.
```

Delete those three comment lines. The guarded bodies they describe have all been deleted.

- [ ] **Step 11.4: Build**

```bash
cd /home/tbaldrid/oss/mora
xmake build mora 2>&1 | tail -10
```

Expected: clean build.

### Task 12: Delete the Windows SKSE block in `xmake.lua`

**Files:**
- Modify: `xmake.lua`

This deletes the entire trailing `if is_plat("windows") then ... end` block that defines `commonlibsse_ng`, `spdlog_rt`, `mora_runtime`, `mora_test_harness` (was lines 262–438 before prior edits). Keep the earlier Windows-CLI block near the top of the file (lines 83–154), which is still needed for Windows cross-compilation of the CLI.

- [ ] **Step 12.1: Identify the block to delete**

The block starts with the comment header:

```lua
-- ══════════════════════════════════════════════════════════════
-- Windows: SKSE Runtime DLL + test harness (cross-compile)
```

…and ends at the matching `end -- windows` closing the outer `if is_plat("windows") then`.

- [ ] **Step 12.2: Delete the block**

Delete from the comment header line `-- Windows: SKSE Runtime DLL + test harness (cross-compile)` (and its surrounding `-- ══…` banner line) through the final `end -- windows` line at the end of the file.

Result: `xmake.lua` ends right after the last test-discovery `end -- not windows` block and has no SKSE-related content.

- [ ] **Step 12.3: Build the full default target set**

```bash
cd /home/tbaldrid/oss/mora
xmake 2>&1 | tail -10
```

Expected: clean build. No Windows-only targets are touched on Linux anyway.

### Task 13: Delete runtime scripts

**Files:**
- Delete: `scripts/deploy_runtime.sh`
- Delete: `scripts/test_runtime.sh`
- Delete: `scripts/MoraRuntime.def`

- [ ] **Step 13.1: Delete the scripts**

```bash
cd /home/tbaldrid/oss/mora
git rm scripts/deploy_runtime.sh scripts/test_runtime.sh scripts/MoraRuntime.def
```

Expected: 3 files removed.

- [ ] **Step 13.2: Confirm remaining scripts are kept**

```bash
ls scripts/
```

Expected: `git-restore-mtime.py` only (the CI mtime helper).

### Task 14: Remove CI steps for the deleted DLL targets

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 14.1: Find the runtime-build lines**

```bash
cd /home/tbaldrid/oss/mora
grep -nE "mora_runtime|mora_test_harness" .github/workflows/ci.yml
```

Expected: two or three matches (the `xmake build -y mora_runtime` and `xmake build -y mora_test_harness` lines, plus a comment about `src/rt/plugin_entry.cpp`).

- [ ] **Step 14.2: Open the ci.yml and read the context around those lines**

Read the 30 lines around each match to understand the surrounding step(s). Typically there will be a whole "Build runtime DLL" step (with a name, shell block, and possibly working-directory / condition) that should be removed wholesale; the comment referencing `src/rt/plugin_entry.cpp` can also be removed since the code it documents is gone.

- [ ] **Step 14.3: Remove the runtime-build step and the stale comment**

Delete the entire "Build runtime DLL" / "Build test harness" step block(s) containing `xmake build -y mora_runtime` and `xmake build -y mora_test_harness`. Also delete the surrounding comment that mentions `src/rt/plugin_entry.cpp`.

- [ ] **Step 14.4: Verify with a grep**

```bash
cd /home/tbaldrid/oss/mora
grep -nE "mora_runtime|mora_test_harness|src/rt/" .github/workflows/ci.yml
```

Expected: no matches.

### Task 15: Final build + test gate

- [ ] **Step 15.1: Clean rebuild**

```bash
cd /home/tbaldrid/oss/mora
xmake clean -a
xmake f -p linux -m debug
xmake build 2>&1 | tail -20
```

Expected: clean full build with no warnings.

- [ ] **Step 15.2: Full test run**

```bash
cd /home/tbaldrid/oss/mora
xmake test 2>&1 | tail -30
```

Expected: all tests pass. Test count is lower than baseline (step B3) by exactly the sum of `tests/dag/` (4), `tests/rt/` (14), `tests/cli/test_dag_section_emitted.cpp` (1) = 19 fewer tests.

- [ ] **Step 15.3: Count LOC deleted**

```bash
cd /home/tbaldrid/oss/mora
git diff --shortstat master..HEAD
```

Expected: net deletion in the low thousands of lines (spec estimate: ~3,000 LOC deleted).

- [ ] **Step 15.4: Sanity-check nothing references the deleted modules**

```bash
cd /home/tbaldrid/oss/mora
git grep -nE "mora/dag/|mora/rt/|mora/harness/|mora/codegen/|src/dag/|src/rt/|src/harness/|src/codegen/|mora_runtime|mora_test_harness|commonlibsse_ng|spdlog_rt|MoraRuntime\.def" \
  -- ':!docs' ':!*.md'
```

Expected: no matches (excluding docs and markdown, which can reference history).

If any matches surface in live source or build files, investigate each — it's either a missed deletion (fix here) or a genuine coupling we didn't plan for (stop and report).

### Task 16: Commit milestone 2

- [ ] **Step 16.1: Review the diff**

```bash
cd /home/tbaldrid/oss/mora
git status
git diff --stat | tail -20
```

Expected: large deletion summary, a handful of modifications (`src/main.cpp`, `xmake.lua`, `.github/workflows/ci.yml`), no additions.

- [ ] **Step 16.2: Stage and commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
mora v3: delete dag, rt, harness, codegen subsystems

Removes ~3,000 LOC across:
  * src/dag/ + include/mora/dag/ + tests/dag/
  * src/rt/ + include/mora/rt/ + tests/rt/
  * src/harness/ + include/mora/harness/
  * src/codegen/address_library.cpp + include/mora/codegen/
  * scripts/deploy_runtime.sh, test_runtime.sh, MoraRuntime.def
  * xmake.lua: commonlibsse_ng, spdlog_rt, mora_runtime,
    mora_test_harness targets; mora_lib file-list entries for
    the deleted subsystems
  * .github/workflows/ci.yml: runtime DLL + test harness steps
  * tests/cli/test_dag_section_emitted.cpp (only dag test outside
    tests/dag)

src/main.cpp drops its dag-compile block and passes an empty
dag_payload to serialize_patch_table. The DagBytecode section
in emitted mora_patches.bin is always empty after this commit.

The v3 architecture (see docs/superpowers/plans/2026-04-17-mora-v3-plan-1-foundation.md
and /home/tbaldrid/.claude/plans/we-need-to-think-iridescent-abelson.md)
replaces dynamic rules with pure batch datalog + parquet snapshot
output, and moves all Skyrim-specific concerns into an extension.
These subsystems are no longer part of that architecture.

Part 1 of the v3 rewrite.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 16.3: Verify the commit**

```bash
git log --oneline master..HEAD
git log -1 --stat | tail -30
```

Expected: two commits on the branch (scaffold + big deletion), the second showing a large net deletion.

---

## Done

After Task 16, the branch is ready for the next plan (introduces `DataSource` and the parquet Sink, extracts ESP into skyrim_compile, etc. — see spec's step 3+). Push the branch and open a draft PR so CI runs on the deletion commit.

```bash
git push -u origin mora-v3-foundation
```

Expected: remote branch created; CI runs on the PR.

**What's next (for awareness, not this plan):**
Plan 2 will cover spec steps 3–5: extract `src/esp/` + `plugin_facts` into `extensions/skyrim_compile/`, introduce the `DataSource` and `Sink` interfaces, wrap the ESP reader as the first `DataSource`, and wrap the existing `src/emit/` patch writer as the first `Sink` (temporary — deleted in a later plan once parquet replaces it).
