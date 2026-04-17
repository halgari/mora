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

With auto-detection, `mora compile` places `mora_patches.bin` in
`Data/SKSE/Plugins/` for you. Drop `MoraRuntime.dll` next to it and
launch the game. That's it.

## Why Mora?

- **Static-first.** Anything derivable from ESP data is fully evaluated at
  compile time. The patch file is literally a sorted table of
  `(FormID, field, op, value)` tuples; applying a patch is one memory write.
- **Dynamic when it has to be.** `maintain` rules track truth values over
  live game state and auto-retract effects when conditions stop holding.
  `on` rules fire edge-triggered on events.
- **Unified vocabulary.** Five built-in namespaces — `form/`, `ref/`,
  `player/`, `world/`, `event/` — cover record data, live references,
  player state, global state, and events, with explicit bridges between
  them.
- **Metaprogrammed.** A single constexpr relation table drives the parser,
  type checker, runtime dispatch, and these docs. New relations are an
  additive one-line change.

## Get Started

- [Getting Started](getting-started.md) — install, write your first rule,
  compile it, run it in-game.
- [How Mora Works](how-mora-works.md) — the three-tier rule model, the
  `.mora.patch` binary format, the runtime engine.
- [Language Guide](language-guide.md) — a walk-through tutorial covering
  syntax, types, verbs, `maintain`/`on`, arithmetic, and examples.
- [Language Reference](language-reference.md) — types, verbs, operators,
  built-in functions.
- [Relation Reference](relations.md) — the full inventory of built-in
  relations, auto-generated from `data/relations/**/*.yaml`.
- [CLI Reference](cli-reference.md) — every `mora` subcommand and flag.
- [Examples](examples.md) — annotated `.mora` files, including the bandit
  bounty rule.
