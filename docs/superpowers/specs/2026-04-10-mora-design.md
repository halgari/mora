# Mora: A Unified Declarative Patching Language for Skyrim

## Problem Statement

Skyrim modding startup times have become unacceptable. A typical Wabbajack modlist (500-2000+ mods) takes 5-6+ minutes to reach the main menu. The root cause: an explosion of independent runtime patchers (SPID, KID, SkyPatcher, FLM, OAR, Smart Talk, OStim, and 70+ others), each implemented as its own SKSE C++ plugin with its own INI parser, its own full scan of all game forms, running sequentially at startup.

These runtime patchers exist because of fundamental limitations in Skyrim's plugin system:

- **Record-level overrides**: When two ESPs modify the same record, last-loaded wins for the *entire record*, not per-field. This makes static compatibility patches combinatorially explosive.
- **254 plugin slot limit**: The single-byte load order index caps full plugins at 254. ESL light plugins allow 4096 but with a 4096-record cap each.
- **No field-level merging**: The engine has no mechanism to merge changes from different mods that touch different fields of the same record.

Runtime patchers bypass all of these by modifying game data in memory after load, using config files (INIs) instead of ESPs. But each patcher independently scans O(rules x records), and with dozens of patchers this becomes the dominant startup cost.

## Solution

**Mora** is a declarative patching language and toolchain that replaces all independent runtime patchers with a single unified system. It has two components:

1. **Mora Compiler** (CLI tool): Reads `.mora` rule files (and imports existing SPID/KID/SkyPatcher INIs), evaluates rules against the loaded ESP data offline, and produces pre-computed binary patch files. Runs once when load order changes.

2. **Mora Runtime** (single SKSE plugin): Applies pre-computed patches at game startup (fast sequential apply, no scanning), and evaluates residual dynamic rules for instance-level events.

The key insight: most patches target base records (weapon definitions, NPC templates, keyword assignments) which are fully known at compile time. Only a small fraction require runtime instance data (NPC current location, equipped items, quest state). By pre-computing the static majority, startup becomes a fast binary patch apply instead of thousands of rule evaluations.

## Language Design

### Syntax: Pythonic Datalog

Mora is a Datalog variant with Python-inspired syntax. Rules are indentation-scoped, body clauses are one-per-line and implicitly conjoined (AND), and `=>` separates the query from effects.

```python
# comments with hash

# directives
requires mod("Sacrosanct.esp")

# derived rules (composable — use in other rules as facts)
bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

# rules with effects
vampire_bane(Weapon):
    weapon(Weapon)
    has_keyword(Weapon, :WeapMaterialSilver)
    not has_keyword(Weapon, :WeapTypeGreatsword)
    => add_keyword(Weapon, :VampireBane)

# conditional effects
bandit_weapons(NPC):
    bandit(NPC)
    level(NPC, Level)
    Level >= 20 => add_item(NPC, :SilverSword)
    Level < 20  => add_item(NPC, :IronSword)

# rules that work for both records and instances
# compiler freezes what it can, defers the rest
bandit_armor(NPC):
    bandit(NPC)
    => add_keyword(NPC, :ArmorHeavyUser)
```

### Syntax Elements

- **Variables**: `UpperCamelCase` — `NPC`, `Weapon`, `Level`
- **Symbols/Keywords**: `:PrefixedWithColon` — `:ActorTypeNPC`, `:BanditFaction`
- **Rule names**: `snake_case` — also serve as derived relation names
- **Negation**: `not` keyword — `not has_keyword(W, :Foo)`
- **Effects separator**: `=>` — left side is query, right side is action
- **Conditional effects**: guard expression on same line as `=>`
- **Built-in comparisons**: `>=`, `<=`, `==`, `!=`, `>`, `<`
- **Built-in arithmetic**: `+`, `-`, `*`, `/` (in guards only)
- **Discard variable**: `_` — `base_level(NPC, _)` binds but ignores

### Rule Composition (Instead of Macros)

Datalog's natural composition replaces the need for macros. Define a derived relation once, reuse it everywhere:

```python
bandit(NPC):
    npc(NPC)
    has_faction(NPC, :BanditFaction)

high_level(NPC):
    base_level(NPC, Level)
    Level >= 30

# compose freely
elite_bandit(NPC):
    bandit(NPC)
    high_level(NPC)
```

### What's NOT in the Language (Phase 1)

- No function definitions
- No loops or imperative control flow
- No string manipulation beyond matching
- No macros
- No LLVM compilation — interpreter/bytecode only

## Data Model

### Namespaces

Every `.mora` file belongs to a namespace. Built-in Skyrim facts live in the `skyrim` namespace.

```python
namespace my_mod.patches

requires mod("Skyrim.esm")

use skyrim.record     # base record facts
use skyrim.instance   # runtime instance facts
```

Third-party mods declare their own namespaces and facts:

```python
namespace ostim.animation

# fact declarations — phase is inferred from usage, not declared
animation_def(AnimID, String)
scene_graph(SceneID, AnimID, AnimID)
playing_animation(FormID, AnimID)
scene_active(SceneID)
```

The compiler determines whether facts are static or dynamic based on how rules use them — specifically, whether the rule's effects target base records or instances.

Import rules:

```python
use requiem.combat                       # import all
use requiem.combat only [is_lethal]      # selective
requiem.combat.damage_mult(NPC, Mult)    # fully qualified inline
```

### Phase Inference

Rules are classified as **static** (freezable) or **dynamic** (runtime) automatically based on what they write to:

- A rule whose `=>` actions modify base records → **static** (frozen into `.spatch`)
- A rule whose `=>` actions modify instances, or whose body reads instance facts → **dynamic** (compiled to bytecode)

The same rule can apply to both base records (at freeze time) and instances (at runtime). The compiler evaluates it against all known base records during the freeze step, and the runtime evaluates it for instances the freezer couldn't see (dynamically spawned NPCs, etc.).

No annotations needed. The compiler reports classifications:

```
$ mora compile

vampire_bane:      fully static -> frozen (14,832 weapons patched)
bandit_weapons:    fully static -> frozen (3,201 NPCs patched)
merchant_goods:    dynamic -> deferred to npc_load (reads current_location)
```

### FormID References

```python
# symbolic (preferred — resolved by EditorID lookup)
add_keyword(NPC, :ActorTypeNPC)

# explicit (plugin-relative, load-order independent)
add_keyword(NPC, :Skyrim.esm|0x013BB9)
```

Symbolic references are resolved at freeze time for static rules and at `DataLoaded` for dynamic rules.

### Deterministic ID Generation

When rules create new data, IDs are deterministic:

```
new_id = hash(rule_name, target_formid, mod_file_path, input_values)
```

Same inputs produce the same ID across restarts. No persistence needed. No save corruption from unstable IDs.

## Type System

### Strong Static Types

Every fact, variable, and expression has a known type at compile time. No runtime type errors.

```python
# the compiler infers types from usage:
bad_rule(NPC):
    npc(NPC)                          # NPC: NpcID
    base_level(NPC, Level)            # Level: Int
    name(NPC, Level)                  # ERROR: Level is Int, name expects String
```

### Type Hierarchy

```
Type
+-- FormID
|   +-- NpcID
|   +-- WeaponID
|   +-- ArmorID
|   +-- KeywordID
|   +-- FactionID
|   +-- SpellID
|   +-- PerkID
|   +-- QuestID
|   +-- LocationID
|   +-- CellID
|   +-- RaceID
+-- Int
+-- Float
+-- String
+-- Bool
+-- List<T>
```

FormID subtypes prevent mixing up record types:

```python
wrong(NPC):
    npc(NPC)
    has_faction(NPC, :IronSword)    # ERROR: IronSword is WeaponID,
                                    #        has_faction expects FactionID
```

### Compile-Time Checks

- **Type inference and validation** on all variables
- **Unresolved reference detection** with fuzzy suggestions for typos
- **Empty match warnings** when a rule matches 0 records in the current load order
- **Unused variable warnings** (use `_` to discard)
- **Duplicate rule detection** within the same namespace
- **Cycle detection** in rule dependencies (warning — Datalog handles it, but may be unintentional)
- **Stratification check** for negation through recursion
- **Shadowing warnings** when a local rule name shadows an imported one
- **Dead rule detection** when a rule depends on facts no mod provides
- **Missing mod detection** for `requires` directives

### Compiler Error Quality

Errors are precise, contextual, and suggest fixes (inspired by Rust/Elm):

```
error[E012]: type mismatch in rule 'bad_rule' (weapons.mora:14:5)

   14 |    has_faction(NPC, :IronSword)
      |                     ^^^^^^^^^^

   Expected: FactionID
   Found:    WeaponID (Skyrim.esm|0x00012EB7)

   :IronSword is a weapon, not a faction. Did you mean :BanditFaction?

   Hint: has_faction(FormID, FactionID) is declared in skyrim.record
```

```
error[E031]: unresolved reference ':SilvrSword' (weapons.mora:22:18)

   22 |    => add_item(NPC, :SilvrSword)
      |                      ^^^^^^^^^^

   No record with EditorID 'SilvrSword' found in load order.

   Similar names:
     :SilverSword (Skyrim.esm|0x0010AA19) -- weapon
     :SilverGreatsword (Skyrim.esm|0x0010C6FB) -- weapon
```

```
warning[W003]: rule 'bandit_gear' matches 0 records (npcs.mora:8)

    8 | bandit_gear(NPC):
    9 |     npc(NPC)
   10 |     has_faction(NPC, :BanditFaction)
   11 |     has_keyword(NPC, :ActorTypeGhost)

   No NPCs in your load order have both BanditFaction and
   ActorTypeGhost. This rule will never fire.

   Hint: did you mean 'not has_keyword' (negation)?
```

## Architecture

### Pipeline Overview

```
  .mora files + INI imports
          |
   +------+-------+
   | Mora Compiler |  (C++ CLI tool, runs offline)
   |               |
   | 1. Parse .mora files + imported INIs
   | 2. Resolve namespaces & symbolic references
   | 3. Type check all rules
   | 4. Evaluate static rules against ESP data
   | 5. Emit:
   |    a. .spatch  (frozen base record patches)
   |    b. .mora.rt (residual runtime rules)
   |    c. diagnostics report
   +------+--------+
          |
     +----+----+
     |         |
  .spatch   .mora.rt
     |         |
     +----+----+
          |
   +------+--------+
   | Mora Runtime   |  (single SKSE plugin)
   |                |
   | At DataLoaded:
   |   1. Load .spatch, apply field-level
   |      diffs to base records in memory
   |   2. Load .mora.rt residual rules
   |   3. Run residual rules against base
   |      records (catches stale-cache cases)
   |
   | On instance events (NPC load, etc):
   |   4. Evaluate dynamic rules against
   |      the loaded instance
   +----------------+
```

### Components

**1. Mora Compiler (C++ CLI tool)**
- Reads `.mora` files, imports SPID/KID/SkyPatcher INIs
- Reads ESPs/ESMs/ESLs via its own parser library (no game dependency)
- Full compiler pipeline: lex, parse, name resolve, type check, lint, phase classify, evaluate/emit
- Outputs `.spatch` + `.mora.rt` + diagnostics
- Triggered by mod manager when load order changes, or manually

**2. Mora Runtime (C++ SKSE DLL)**
- Single SKSE plugin replacing dozens of distributors
- Loads `.spatch` — fast sequential apply, no scanning
- Loads `.mora.rt` residual rules into a lightweight bytecode VM
- Registers for SKSE events to trigger dynamic rule evaluation
- Deterministic ID generation via `hash(rule, target, inputs)`

**3. INI Importers**
- Translators from SPID `_DISTR.ini`, KID `_KID.ini`, SkyPatcher INI formats into Mora rules
- Run as part of the compiler pipeline
- Existing INI files work without modification

**4. ESP Parser Library (C++)**
- Reads Bethesda plugin files to extract base records
- Used by the CLI compiler (runs outside the game)
- Shared code with the runtime where possible (CommonLibSSE for runtime, custom parser for CLI)

### Compiler Architecture

```
Source (.mora files)
    |
    v
Lexer          -> Token stream
    |
    v
Parser         -> Concrete Syntax Tree (lossless, preserves whitespace/comments)
    |
    v
Lower          -> AST (typed, desugared)
    |
    v
Name Resolve   -> resolve namespaces, imports, symbolic :EditorID refs
    |
    v
Type Check     -> infer & validate all variable types, report errors
    |
    v
Lint/Warn      -> unused vars, empty matches, suspicious patterns
    |
    v
Phase Classify -> tag each rule as static or dynamic
    |
    v
Evaluate/Emit  -> static: evaluate against ESP data -> .spatch
                -> dynamic: compile to bytecode -> .mora.rt
```

**Compiler properties:**
- **Lossless CST**: Parser preserves everything for future tooling (formatter, IDE support, refactoring)
- **Fast**: Single-pass type inference, parallel file parsing, incremental recompilation via file-level dependency hashing
- **All errors reported**: Collect and report all errors, don't stop at the first
- **Incremental**: If only one `.mora` file changed, re-check and re-evaluate only that file and its dependents
- **C++ throughout**: Shared codebase between compiler and runtime, arena allocators for AST nodes, interned string table for EditorID comparisons

## Conflict Resolution

Field-level last-write-wins. When multiple rules from different mods modify the same field of the same record, load order determines the winner:

```
1. Load order of the parent ESP/mod (same as Skyrim's plugin.txt)
2. Within the same mod, file order (alphabetical .mora files)
3. Within the same file, rule order (top to bottom)
```

If mod A sets damage and mod B sets name on the same weapon, both apply — no conflict. Only when both set damage does load order break the tie.

The compiler produces a conflict report:

```
$ mora compile

Conflicts resolved (last-write-wins):
  BanditMelee01 (Skyrim.esm|0x0003B547)
    damage: 12 (Skyrim.esm) -> 18 (Requiem) -> 15 (MyMod)   <- MyMod wins
    name:   "Bandit" (Skyrim.esm) -> "Marauder" (Requiem)    <- Requiem wins

No unresolved conflicts.
Frozen 14,832 patches. 47 rules deferred to runtime.
```

## Binary Formats

### .spatch (Frozen Patches)

Optimized for sequential read and bulk apply. The runtime loads it, walks it front to back, and writes values into game memory.

```
Header:
    magic:              "MORA" (4 bytes)
    version:            u16
    load_order_hash:    u64
    source_hash:        u64
    patch_count:        u32
    string_table_offset: u64

String Table:
    count:   u32
    entries: [length: u16, utf8_bytes: ...]

Patch Entries (sorted by target FormID for lockstep traversal):
    target_formid:  u32 (plugin-relative, resolved at load)
    source_plugin:  u16 (string table index -> plugin name)
    field_count:    u16
    fields: [
        field_id:    u16 (enum: name, damage, keywords, etc.)
        op:          u8  (set, add, remove)
        value_type:  u8  (int, float, string, formid, etc.)
        value:       variable length
    ]
```

Properties:
- Sorted by FormID for lockstep application with game records
- Plugin-relative FormIDs resolved to runtime FormIDs using actual load order
- Field-level ops: `set` overwrites, `add` appends to collections, `remove` strips from collections
- Already conflict-resolved — only winning values present
- Compact — typically a few MB for a 1000-mod setup

### .mora.rt (Residual Runtime Rules)

Bytecode for a simple stack-based VM:

```
Header:
    magic:       "MORT" (4 bytes)
    version:     u16
    rule_count:  u32

Rule Entry:
    name_idx:     u16 (string table index)
    trigger:      u8  (enum: on_data_loaded, on_npc_load,
                        on_cell_change, on_equip, on_quest_update)
    clause_count: u16
    clauses:      [bytecode]
    action_count: u16
    actions:      [bytecode]
```

The trigger is derived by the compiler from the instance facts the rule references.

### Stale Cache Detection

```
.mora.lock contains:
    load_order_hash:  hash of plugin list + order
    source_hash:      hash of all .mora source files
    timestamp

At DataLoaded:
    if hash mismatch:
        log warning "cache is stale, running full evaluation"
        fall back to interpreting all rules at runtime
```

The game always works. A stale cache means slower startup (but still faster than 70 separate plugins), not broken behavior.

## Project Structure

A mod using Mora:

```
MyMod/
+-- MyMod.esp              # normal ESP (if the mod has its own records)
+-- mora/
|   +-- mod.mora           # manifest (namespace, dependencies, INI imports)
|   +-- weapons.mora       # weapon patch rules
|   +-- npcs.mora          # NPC distribution rules
|   +-- leveled_lists.mora # leveled list rules
+-- SKSE/
    +-- Plugins/
        # no per-mod DLL needed
```

The `mod.mora` manifest:

```python
namespace my_mod.patches

requires mod("Skyrim.esm")
requires mod("Sacrosanct.esp")

import_spid "SPID_DISTR.ini"
import_kid  "KID_Config.ini"
```

Compiler output:

```
MoraCache/
+-- mora.spatch   # frozen binary patches
+-- mora.rt       # residual runtime rules (bytecode)
+-- mora.lock     # staleness detection hashes
+-- mora.log      # diagnostics report
```

## Evolution Path

### Phase 1 (This Spec): Two-Phase Interpreter
- Simple rule interpreter, no partial freezing
- A rule is either fully static or fully dynamic
- Still delivers the core performance win: single unified scan, pre-computed base record patches

### Phase 2: Proper Datalog Engine
- Semi-naive bottom-up evaluation
- Partial freezing: static portions of mixed rules pre-computed, residual deferred
- Better optimization of rule evaluation order

### Phase 3: LLVM Compilation
- Compile hot evaluation paths to native code via LLVM
- JIT compilation for dynamic rules
- FFI for importing external libraries — mod authors can extend without C++ SKSE plugins

### Phase 4: Incremental / Differential Evaluation
- Differential Datalog-style incremental updates
- When game state changes, only recompute affected derived facts
- Enables efficient mid-game reactivity
