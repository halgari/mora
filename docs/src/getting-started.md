# Getting Started

This guide walks you through installing Mora, writing your first `.mora`
file, compiling it into a patch file, and loading it into Skyrim. By the end
you will have a working setup that applies patches at game load.

---

## Prerequisites

**Skyrim Special Edition (Steam).** Mora targets SSE. The Game Pass version
is not supported because it does not allow SKSE.

**SKSE64.** Mora's runtime DLL is loaded by SKSE. Download and install from
[skse.silverlock.org](https://skse.silverlock.org); match the version to
your Skyrim build.

**Address Library for SKSE Plugins.** Install from
[Nexus Mods (mod 32444)](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

**Mora itself** — see below.

---

## Building Mora

Mora is built from source. The compiler runs on Linux (or any POSIX host);
the runtime DLL is produced by a cross-compiled build using `clang-cl` +
`lld-link` and the Windows SDK via `xwin`. No Windows machine required.

### Build dependencies

- [xmake](https://xmake.io) — build system
- `clang-cl` and `lld-link` — Clang's MSVC-compatible driver and linker
- Windows SDK headers via [xwin](https://github.com/Jake-Shadle/xwin)

Once those are on `PATH`:

```bash
git clone https://github.com/halgari/mora.git
cd mora

# Build the host-side compiler.
xmake build mora

# Build and test.
xmake build && xmake test

# The compiler binary is at build/linux/x86_64/release/mora
```

Add the `mora` binary to your `PATH`, or invoke it by its full path for the
rest of this guide.

---

## Your First `.mora` File

Create `balance.mora` anywhere:

```mora
namespace my_mod.balance

use form :as f

# All iron weapons get boosted damage.
iron_weapons(W):
    f/weapon(W)
    f/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)
```

Line by line:

`namespace my_mod.balance`
: Every file starts with a namespace declaration. Reverse-domain style is a
good convention, but anything unique works.

`use form :as f`
: Clojure-style namespace import. Aliases `form` to `f` so we can write
`f/weapon` instead of `form/weapon`. `:refer [...]` is the other form,
for bringing specific names in unqualified.

`iron_weapons(W):`
: Rule head. `W` is a logic variable — unbound until the first clause
binds it.

`f/weapon(W)`
: Predicate. True when `W` is a weapon base record (a WEAP).

`f/keyword(W, @WeapMaterialIron)`
: List-valued relation; in body position it's a query. `@WeapMaterialIron`
is a compile-time EditorID reference — Mora resolves it against your ESPs
and rejects the compile if the keyword isn't defined.

`=> set form/damage(W, 20)`
: Head effect. `form/damage` is a `countable<Int>`; legal verbs are `set`,
`add`, `sub`. `set` pins base damage to 20 on every matched weapon.

This rule is **static**: all body relations are in the `form/*` namespace,
which is fully known at compile time. The compiler evaluates it completely
and emits the resulting patches to the binary output; nothing about this
rule runs at game time beyond applying the patches.

See [language-guide.md](language-guide.md) for the full tour, and
[relations.md](relations.md) for the catalog of relations you can query.

---

## Compiling

Point `mora compile` at your file and your Skyrim Data directory:

```bash
mora compile balance.mora --data-dir "/path/to/Skyrim Special Edition/Data"
```

On a typical Steam install on Linux that's
`~/.steam/steam/steamapps/common/Skyrim Special Edition/Data`.

Expected output:

```
  Mora v0.1.0

  [OK] Parsing 1 files
  [OK] Resolving 1 rules
  [OK] Type checking 1 rules
  [OK] 1 static, 0 dynamic
  [OK] 15 plugins, 3 relations -> 59522 facts
  [OK] Evaluating (.mora rules) done
  [OK] 200 patches -> mora_patches.bin (4.1 KB)
  [OK] Wrote MoraCache/mora_patches.bin
```

`1 static, 0 dynamic`
: Phase classifier output. Unannotated rules whose bodies touch only static
namespaces are static. Rules referencing `ref/*`, `player/*`, `world/*`,
or `event/*` must be annotated `maintain` or `on`.

`15 plugins, 3 relations -> 59522 facts`
: ESP load summary: how many plugins loaded, how many fact relations the
compiler needed to extract, and the total fact count.

`200 patches -> mora_patches.bin`
: The Datalog engine found 200 weapons that matched all clauses. Each one
gets a 16-byte patch entry in the binary output.

The output file is `MoraCache/mora_patches.bin` by default (override with
`--output DIR`).

If you'd like to see exactly what patches would be produced without touching
the filesystem, use [`mora inspect`](cli-reference.md#mora-inspect).

!!! warning
    If you see `form not found: @WeapMaterialIron`, the keyword's EditorID
    isn't present in any loaded plugin. Double-check the spelling and make
    sure the plugin that defines it is in your Data directory.

---

## Deploying

Copy both the patch file **and** the runtime DLL into SKSE's plugin folder:

```
Data/SKSE/Plugins/
  mora_patches.bin     <-- from MoraCache/
  MoraRuntime.dll      <-- from the xmake build
```

Launch the game through SKSE (via `skse64_loader.exe` or your mod manager's
SKSE option). Launching plain `SkyrimSE.exe` will not load SKSE plugins.

!!! tip "Mod Organizer 2 users"
    Deploy both files into a mod folder under the same `SKSE/Plugins/`
    path. MO2 will virtualize them into the Data directory automatically.

---

## Verifying

On load, the runtime writes a log file at:

```
Data/SKSE/Plugins/MoraRuntime.log
```

You should see something like:

```
[Mora] mmap'd mora_patches.bin (4.1 KB)
[Mora] applied 200 patches in 0.42 ms
[Mora] dynamic rules: 0 (nothing to register)
```

If the file is missing, SKSE didn't load the plugin. Check `skse64.log` in
the same folder for SKSE's own diagnostics.

---

## Next Steps

- [Language Guide](language-guide.md) — negation, disjunction, arithmetic,
  `maintain` and `on` rules, namespaces, `:keyword` tag values.
- [Examples](examples.md) — annotated `.mora` files including the bandit
  bounty capstone.
- [How Mora Works](how-mora-works.md) — the architectural overview: why
  Mora splits rules across static/maintain/on, and how the runtime loads
  the patch file.
- [Relation Reference](relations.md) — the full inventory of built-in
  relations, auto-generated from the YAML declarations.
