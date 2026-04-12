# How Mora Works

Mora is a declarative patching language for Skyrim Special Edition. You write rules describing what you want: which NPCs get which spells, which weapons get which keywords, what damage values should be. Mora evaluates those rules once, at compile time, against your actual plugin data, and bakes the results into a native x86-64 DLL.

At runtime: SKSE loads the DLL. The DLL calls `apply_all_patches()`. Every patch is a single memory write at a known offset. No searching. No rule evaluation. No INI parsing. Done.

> **6,782 NPC name patches applied in 1.63 milliseconds.**

That number is measured output from the logging in `plugin_entry.cpp`, captured via `QueryPerformanceCounter` on actual hardware. The runtime cost reflects writing to memory sequentially, with nothing to optimize away.

---

## The Problem

Skyrim modding has a startup problem.

A large Wabbajack modlist (500 to 2,000 mods) can take five to ten minutes to reach the main menu. The cause is dozens of independent runtime patchers running sequentially, each doing the same categories of work:

1. Parse their own INI files
2. Scan every form in the game matching their target types
3. Evaluate distribution rules against each form
4. Apply matching patches to memory

Each patcher is its own SKSE plugin. Each reimplements form iteration, condition checking, and patching logic. Each runs a full pass over thousands of game records. And they run every single time you launch the game, repeating identical work against identical data.

The underlying data (the ESPs, the ESMs, the keywords, the factions, the base record fields) does not change between launches. But nobody caches the results.

Mora caches the results. Permanently. In compiled native code.

---

## Comparison with Existing Approaches

### SPID: Spell Perk Item Distributor

SPID distributes spells, perks, items, shouts, and packages to NPCs via `_DISTR.ini` files. It runs at `DataLoaded`, iterating over every NPC in the game and evaluating every configured rule against each one.

The complexity is **O(NPCs × rules)**. With 50 SPID configs touching 6,000+ NPCs, that is 300,000+ rule evaluations per launch. Each evaluation involves string matching, faction checks, keyword checks, level range comparisons, and mod-present checks.

SPID is fast for small setups. It does not scale. Every evaluation happens at runtime, from scratch, every launch.

### KID: Keyword Item Distributor

KID follows the same runtime model as SPID but distributes keywords to forms instead of items to NPCs. The scaling story is identical: **O(forms × rules)** evaluated at every launch with no caching between sessions.

### SkyPatcher

SkyPatcher handles a broader range of record types (25+) via INI files. It is more flexible than SPID or KID but follows the same architectural pattern: per-form INI evaluation at startup.

With hundreds of SkyPatcher INI files active, that is hundreds of passes over form data. Each pass is essentially a database scan with no index. Flexibility without caching compounds the startup problem rather than solving it.

### Synthesis (Mutagen-based)

Synthesis takes a fundamentally different approach: generate ESP/ESM patch files ahead of time rather than patching at runtime. This is architecturally sound. The runtime cost is essentially zero, because the patches are baked into plugins that the engine loads normally.

But Synthesis has real costs of its own:

- **Patch plugins can be enormous.** A large Synthesis run can produce a 100MB+ ESP that consolidates thousands of overrides.
- **Re-run required on any load order change.** Adding or removing a mod, reordering plugins, or updating an existing mod means running the full Synthesis pipeline again, often taking many minutes per patcher.
- **Each patcher is a separate C# program.** There is no shared abstraction. Every patcher reimplements its own record access logic using the Mutagen library. The toolchain requires .NET.
- **Results live in a separate plugin.** Synthesis output is an ESP that the engine loads alongside your other plugins. It consumes a plugin slot and must be kept in sync manually.

Synthesis is the right idea, compute at build time rather than runtime, applied through a framework that trades one set of complexity for another.

### Custom SKSE Plugins

Writing a custom SKSE plugin in C++ gives you maximum control. You can access any game structure, hook any function, and apply any transformation. But:

- Every patcher requires original C++ development.
- Every patcher reimplements form lookup, memory layout knowledge, and field offsets, all of which are fragile across game updates.
- There is no shared abstraction layer. When the game updates, every plugin that hard-coded an offset breaks.
- The barrier to entry is high. Most mod authors do not write C++.

Custom SKSE plugins are the right output format: native, fast, and integrating cleanly with the engine. The problem is the cost to produce them.

### Mora

Mora occupies the position that no existing tool does: compile-time rule evaluation producing a native DLL.

- Rules are evaluated **once**, on your machine, against your actual ESP data.
- Results are frozen into direct memory writes and compiled to LLVM IR.
- The IR is optimized and linked into a native x86-64 DLL.
- At runtime, the DLL calls `apply_all_patches()`, a sequential loop over pre-computed writes.
- Runtime complexity is **O(patches)**: one memory write per patch, no rule evaluation, no form scanning, no INI parsing.
- A single DLL replaces SPID, KID, SkyPatcher, and any custom patchers you would have written.

The DLL must be recompiled when your load order changes, just as Synthesis must be re-run. But it recompiles in seconds, not minutes, and produces a 16KB DLL instead of a 100MB ESP.

### Comparison Table

| | Runtime Cost | Scales With | Re-run Needed | Output |
|---|---|---|---|---|
| SPID / KID | O(forms × rules) | Every launch | Every launch | In-memory |
| SkyPatcher | O(forms × INIs) | Every launch | Every launch | In-memory |
| Synthesis | None (compile-time) | Load order changes | On mod changes | ESP files |
| SKSE plugins | Varies (hand-coded) | N/A | Recompile | DLL |
| **Mora** | **O(patches)** | **Never** | **On mod changes** | **DLL** |

!!! note "What 'O(patches)' means in practice"
    200 weapon damage patches apply in under 1ms. 6,782 NPC name patches apply in 1.63ms. The runtime is sequential memory writes, as fast as a memory write can be. There is nothing to optimize because the work has already been done at compile time.

---

## How It Works: The Pipeline

Here is a complete walkthrough of the journey from `.mora` source file to running DLL.

### Step 1: Write Rules

You write rules in Mora's Datalog-inspired syntax. A rule has a head (what it produces), a body (the conditions that must hold), and an optional effect (what to do when conditions are met):

```mora
namespace my_mod.balance

requires mod("Skyrim.esm")

# Derived relation, reusable in other rules
iron_weapons(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialIron)

# Rule with effect: sets damage on every iron weapon
iron_damage(Weapon):
    iron_weapons(Weapon)
    => set_damage(Weapon, 15)
```

Rules are declarative. You describe what is true, not how to compute it. The engine figures out evaluation order. Composition is free: `iron_weapons` is a derived relation that `iron_damage` can use as if it were a built-in fact.

### Step 2: Compiler Loads ESPs

When you run `mora compile`, the compiler reads your actual plugin data from your Skyrim installation. It parses every ESP, ESM, and ESL in your load order and builds a **FactDB**, an in-memory relational database of all game records.

```
mora compile --data-dir ~/.steam/.../Skyrim Special Edition/Data
```

The FactDB contains everything: all NPCs, weapons, armors, keywords, factions, races, spells. Every field on every record. Every cross-reference between them.

### Step 3: Phase Classification

Not every rule can be evaluated at compile time. Rules that depend on runtime game state (the player's current location, an NPC's equipped items, active quest stages) cannot be resolved until the game is running.

The compiler classifies each rule automatically:

- **Static**: all inputs are known from ESP data. Can be fully evaluated at compile time. Results frozen into the DLL.
- **Dynamic**: depends on runtime state. Deferred to a bytecode VM (future work).

Most rules targeting base records are static. The compiler reports which rules went where:

```
iron_damage:     fully static → frozen (200 weapons patched)
bandit_weapons:  fully static → frozen (3,201 NPCs patched)
merchant_goods:  dynamic → deferred (reads current_location)
```

No annotations required. The compiler infers phase from what the rule reads and writes.

### Step 4: Evaluation Against the FactDB

Static rules are evaluated bottom-up against the FactDB. This is where the real work happens, and it happens once, on your machine, not in Skyrim.

Datalog evaluation is well-understood algorithmically. The compiler joins relations, applies filters, resolves negation using stratification, and produces a set of concrete matches. For `iron_damage`, the evaluation finds every weapon in every loaded ESP that has `:WeapMaterialIron`, then produces a patch entry for each one.

The result of evaluation is a flat list of `(FormID, field, value)` tuples: the patch set.

### Step 5: Patch Generation

The patch set is conflict-resolved (last load-order wins, per field) and deduplicated. The compiler produces a human-readable conflict report so you can see exactly which mod's value won for each field:

```
BanditMelee01 (Skyrim.esm|0x0003B547)
  damage: 12 (Skyrim.esm) → 18 (Requiem) → 15 (MyMod)   ← MyMod wins
```

The final patch set is concrete: specific FormIDs, specific fields, specific values. No conditions. No rules. Just a list of writes.

### Step 6: LLVM Codegen

The patch set is compiled to LLVM IR. Each patch becomes a store instruction at a known base-relative offset:

```llvm
; Pseudocode: actual IR uses base address + offset arithmetic
define void @apply_all_patches(i8* %base) {
    ; Patch 1: IronSword damage = 15
    %addr_0 = getelementptr i8, i8* %base, i64 0x1A2B3C4D
    store i16 15, i16* %addr_0

    ; Patch 2: BanditMelee01 damage = 18
    %addr_1 = getelementptr i8, i8* %base, i64 0x1A2B5E6F
    store i16 18, i16* %addr_1

    ; ... thousands more ...
    ret void
}
```

LLVM optimizes and compiles this to native x86-64. The resulting `apply_all_patches` function is a tight sequence of `mov` instructions, as efficient as a memory write can be.

The runtime glue lives in `plugin_entry.cpp`, a minimal SKSE plugin that registers for `DataLoaded`, calls `apply_all_patches(GetModuleHandleW(nullptr))`, and logs the elapsed time. The DLL is linked from the generated IR plus this glue.

### Step 7: Runtime

Drop `MoraRuntime.dll` in `Data/SKSE/Plugins/`. Launch the game. When `DataLoaded` fires:

```
[Mora] Applied 6782 patches in 1.63 ms
```

There is nothing else. No INI parsing. No form iteration. No rule evaluation. The work is already done.

!!! tip "What happens when your load order changes?"
    Run `mora compile` again. The compiler re-reads your ESPs, re-evaluates your rules against the new data, and produces a new DLL. This takes a few seconds. Drop the new DLL in SKSE/Plugins and you are done.

    The DLL embeds a hash of the load order it was compiled against. If there is a mismatch at runtime (you forgot to recompile), Mora logs a warning. The DLL still applies; it just may be applying stale data.

---

## Why Datalog?

Skyrim's plugin data is a database. Forms are rows. Fields are columns. Cross-references between forms are foreign keys. The game's structure is naturally relational.

Datalog is a query language for relational data. It is also the foundation of static analysis, program synthesis, and constraint solving, well-studied domains with decades of research behind them. Using Datalog for Mora gives several concrete advantages:

**Declarative and composable.** Rules are facts about what is true, not instructions for how to compute. Derived relations compose naturally: write `bandit(NPC)` once and reuse it in every rule that needs it. No copy-paste, no macros.

```mora
bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

high_level(NPC):
    base_level(NPC, Level)
    Level >= 30

# Free composition
elite_bandit(NPC):
    bandit(NPC)
    high_level(NPC)
    => add_item(NPC, :SilverSword)
```

**Guaranteed termination.** Datalog programs always terminate. There are no infinite loops, no accidental recursion that stalls the compiler. Every rule evaluation completes in bounded time.

**Stratified negation.** Datalog supports safe `not` clauses via stratification, a well-defined ordering of rule evaluation that ensures negation is always applied to a complete, stable set of facts. You can write `not has_keyword(Weapon, :WeapTypeGreatsword)` and the compiler guarantees it means what you think it means.

```mora
vampire_bane(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialSilver)
    not has_keyword(Weapon, :WeapTypeGreatsword)
    => add_keyword(Weapon, :VampireBane)
```

**Partial evaluation is well-studied.** The key property that makes Mora's compile-time evaluation tractable is that Datalog rules can be *partially evaluated*: executed against known facts at compile time, with residual unknowns deferred to runtime. This is a formal technique with decades of research behind it. The compiler knows exactly which rules can be fully frozen and which cannot, without guessing.

**The data model is a natural fit.** ESP records are essentially database rows. Querying "all NPCs in BanditFaction with level >= 30 that do not already have SilverSword" is exactly the kind of join-and-filter query Datalog was designed for. You are working with the abstraction, not against it.

!!! note "I don't know Datalog. Does that matter?"
    No. Mora's syntax is Python-inspired and you do not need to know anything about Datalog theory to write rules. The key ideas (conditions in the body, effects after `=>`, derived relations for reuse) are intuitive once you see a few examples. The [Language Guide](language-guide.md) walks through everything from first principles.

---

## The Key Insight

Everything that **can** be known at compile time **is** computed at compile time.

The data in your ESPs is static. The rules you write are static. The relationship between them is deterministic. There is no reason to recompute it at every game launch, yet every existing patcher does exactly that.

Mora moves the computation to where it belongs: your development machine, once, when the rules or load order change. The DLL delivered to the game contains nothing but pre-computed answers.

This is the correct architecture for the problem.
