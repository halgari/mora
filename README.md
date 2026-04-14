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
  figures out the order, the iteration, and the merge. Patches apply
  directly to live forms at load — no generated ESP, no load-order slot,
  no master dependencies to worry about.
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

### Windows — SKSE runtime DLL

The runtime DLL (`MoraRuntime.dll`) is the SKSE plugin that applies patches
in-game. It can be built two ways:

- **Native (GitHub Actions / Windows dev box)** — MSVC via `xmake build
  mora_runtime` or the CI workflow at `.github/workflows/ci.yml`.
- **Cross-compile from Linux** — clang-cl + xwin; see
  `scripts/build_commonlib.sh`, `scripts/build_rt_lib.sh`, and
  `scripts/build_runtime_dll.sh`.

Either way you'll need the `extern/CommonLibSSE-NG` submodule initialised:

```bash
git submodule update --init --recursive
```

## CI

[![CI](https://github.com/halgari/mora/actions/workflows/ci.yml/badge.svg)](https://github.com/halgari/mora/actions/workflows/ci.yml)

- **Linux** — xmake build + full gtest suite.
- **Windows** — MSVC build of CommonLibSSE-NG + `MoraRuntime.dll`, uploaded
  as an artifact.
- **Docs** — MkDocs site auto-published to GitHub Pages on push to `master`.

## Status

Mora is pre-1.0 and under active development. The language surface, patch
file format, and runtime ABI are all subject to change. See the docs for the
current state of each subsystem.

## License

TBD.
