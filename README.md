# Mora

**Declarative datalog for Skyrim Special Edition.**

Mora is a small language for describing how Skyrim's data should look and how
it should react at runtime. You write rules; Mora evaluates everything it can
at compile time against your real ESP/ESM load order and bakes the results
into a flat, memory-mappable patch file. An accompanying SKSE plugin mmaps
that file, applies the static patches on game load, and runs a small
differential-dataflow engine for the rules that have to wait for live game
state.

> **2.5M patches applied in under 10ms, zero runtime rule evaluation.**

Full documentation: **<https://halgari.github.io/mora/>**

## Quick Example

```mora
namespace my_mod.balance

use form :as f

# All iron weapons get 20 damage — fully static, baked into the patch file.
iron_weapons(W):
    f/weapon(W)
    f/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)
```

```bash
$ mora compile balance.mora --data-dir "~/.steam/.../Skyrim Special Edition/Data"
  Mora v0.1.0
  [OK] Parsing 1 files
  [OK] Resolving 1 rules
  [OK] Type checking 1 rules
  [OK] 1 static, 0 dynamic
  [OK] 200 patches -> mora_patches.bin
```

Drop `mora_patches.bin` and `MoraRuntime.dll` in `Data/SKSE/Plugins/` and
launch the game.

## Why Mora?

- **Static-first.** Anything derivable from ESP data is fully evaluated at
  compile time. The patch file is a sorted table of
  `(FormID, field, op, value)` tuples; applying a patch is one memory write.
- **Dynamic when it has to be.** `maintain` rules track truth values over
  live game state and auto-retract effects when conditions stop holding.
  `on` rules fire edge-triggered on events.
- **Unified vocabulary.** Five built-in namespaces — `form/`, `ref/`,
  `player/`, `world/`, `event/` — cover record data, live references,
  player state, global state, and events, with explicit bridges between
  them.

## Mora vs. SynthEE / zEdit / SkyProc / xEdit scripting

If you've built patchers before, Mora occupies a different niche:

- **SynthEE / Synthesis (C# patchers via Mutagen).** Great toolkit, but every
  patcher is an imperative program you have to write, compile, and
  sequence. Mora replaces the program with a set of rules; the compiler
  figures out the order, the iteration, and the merge.

  The deeper difference is the output format. Synthesis emits a real
  Skyrim `.esp`, which means the patch has to live within every limit
  the game engine imposes on plugins:
  - **One of 255 full plugin slots** (or an ESL slot, which caps new
    records at 2048 and FormIDs at 0xFFF). Every Synthesis patcher you
    run burns a slot; stack a few and you're budgeting load order
    around your patchers.
  - **Master dependency graph.** An ESP that overrides records from
    `Skyrim.esm`, `Dawnguard.esm`, and some mod's `.esp` must master
    all of them, in a compatible order. Reorder the load order and the
    patch has to be regenerated.
  - **Record-level conflict resolution.** Two ESPs touching the same
    record conflict on the whole record — the winner's version wipes
    the loser's edits to unrelated fields. You fix it with another
    patch, or with conflict-resolution rules inside Synthesis.
  - **Record schema is fixed.** You can only express things the ESP
    format has a field for. Computed values, cross-record joins, and
    "this effect should retract when X stops being true" don't fit and
    have to be faked with scripts or SPID-style runtime glue.
  - **Regeneration on every load-order change.** Add, remove, or
    reorder a mod and the Synthesis output is stale — you re-run the
    pipeline and ship a new ESP.

  Mora sidesteps the ESP entirely. The output is a flat binary of
  `(FormID, field, op, value)` tuples applied to live forms at load
  by an SKSE plugin. No plugin slot, no masters, no record-level
  conflicts (rules merge at the field/op level), no schema lock-in
  (the rule language has arithmetic, joins, and computed values), and
  dynamic `maintain`/`on` rules for behavior that depends on live game
  state and must auto-retract — none of which an ESP can express.
- **SkyProc.** Abandoned, Java, per-patcher GUIs. Mora is a single binary
  with one input language and a declarative model that doesn't need a GUI
  per rule.
- **zEdit / xEdit scripting (Pascal / JS).** Excellent for ad-hoc surgery,
  but scripts are tied to the editor and produce an ESP you have to ship,
  version, and resolve conflicts on. Mora's output is a flat binary loaded
  by an SKSE plugin — it takes zero plugin slots and conflicts are
  resolved at rule-level, not record-level.
- **SkyPatcher / SPID / KID (INI-driven runtime distributors).** These are
  the closest cousins — also ESP-less, also applied at load by an SKSE
  plugin. The difference is expressive power. Their INIs are flat filter
  lists: "attach keyword X to items matching filters Y". Mora is a real
  language with variables, joins across relations, arithmetic, and
  computed values. You can write "every iron weapon's damage becomes
  `base * 1.2 + player_level`" in one rule; in SPID/KID/SkyPatcher you
  can't express the computation at all. Mora also unifies all three of
  their niches (record edits, distribution, keyword attachment) under one
  vocabulary, and adds dynamic `maintain`/`on` rules for things that
  depend on live game state and must auto-retract.

In short: write rules, not patchers or filter INIs. No ESP slot, no load
order churn, and you get dynamic behavior and computed values for free.

## Repository layout

```
src/         Compiler, evaluator, runtime (C++20/C++23)
  core/        Arenas, string pools, diagnostics
  lexer/       ast/  parser/  sema/      Frontend
  eval/        emit/                     Static evaluator + patch emitter
  rt/          dag/                      SKSE runtime + dataflow engine
  model/                                 Generated relation table
data/relations/  YAML relation specs (source of truth)
include/     Public headers
tests/       gtest suite (per-subsystem + integration)
tools/       gen_relations.py, gen_docs.py (metaprogramming)
scripts/     Build/deploy helpers for the SKSE runtime DLL
docs/        MkDocs site published to halgari.github.io/mora
extern/      CommonLibSSE-NG submodule + spdlog/simplemath shims
```

## Building

### Linux — CLI compiler and tests

```bash
xmake f -y
xmake build
xmake test
```

Requires `xmake` and a C++20 compiler (GCC 13+ or Clang 16+). xmake fetches
gtest, fmt, and zlib automatically.

### Windows — SKSE runtime DLL (cross-compiled on Linux)

The runtime DLL (`MoraRuntime.dll`) is the SKSE plugin that applies patches
in-game. Both local dev builds and CI cross-compile it from Linux using
clang-cl + lld-link + llvm-lib against the xwin-provided Windows SDK — no
Visual Studio, no wine, no Windows host needed.

Prerequisites:

- `clang-cl`, `lld-link`, `llvm-lib` (LLVM ≥ 18 — `pacman -S clang lld llvm`
  on Arch, `apt-get install clang lld llvm-dev` on Debian/Ubuntu).
- An [xwin](https://github.com/Jake-Shadle/xwin) sysroot at `$HOME/.xwin`
  (override via `XWIN_PATH`). One-time setup:
  `xwin --accept-license splat --output $HOME/.xwin`.
- `xmake` ≥ 3.0.
- Submodules: `git submodule update --init --recursive`.

Build:

```bash
xmake f -p windows -a x64 --toolchain=xwin-clang-cl -m release
xmake build mora mora_runtime mora_test_harness
```

Outputs land in `build/windows/x64/release/`:
`mora.exe`, `MoraRuntime.dll`, `MoraTestHarness.dll`.

`xmake.lua` defines a `xwin-clang-cl` toolchain that wraps clang-cl with
xwin's sysroot; see the top of the file for the flag set. For a deeper
walkthrough of the toolchain, the traps we hit getting it working, and
the Proton-based runtime-validation loop, see
[docs/src/cross-compile-windows.md](docs/src/cross-compile-windows.md).

## Editor support

Syntax highlighting and a forthcoming language server live in
[`editors/vscode/`](editors/vscode/). The packaged `.vsix` ships in the
Windows release archive at `Data/tools/Mora/mora-vscode-<version>.vsix`;
sideload it into VS Code with **Extensions → ⋯ → Install from VSIX**.

## CI

[![CI](https://github.com/halgari/mora/actions/workflows/ci.yml/badge.svg)](https://github.com/halgari/mora/actions/workflows/ci.yml)

- **Linux** — xmake build + full gtest suite, plus a cross-compile of
  `mora.exe`, `MoraRuntime.dll`, and `MoraTestHarness.dll` for Windows
  using clang-cl + xwin. The Windows binaries are uploaded as an artifact.
- **Docs** — MkDocs site auto-published to GitHub Pages on push to `master`.

## Status

Mora is pre-1.0 and under active development. The language surface, patch
file format, and runtime ABI are all subject to change. See the docs for the
current state of each subsystem.

## License

[MPL 2.0](LICENSE.md)
