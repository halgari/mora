# Mora v2 — Static + Dynamic Datalog Design

**Date:** 2026-04-14
**Status:** Design approved; ready for implementation planning

## Goals

Reshape Mora around a two-phase datalog model:

- **Static datalog** runs at compile time as a partial evaluator. Everything derivable from ESP data is fully resolved and baked into memory-mappable tables.
- **Dynamic datalog** runs at runtime as a differential dataflow engine, fed by SKSE event deltas and joined against the pre-built static arrangements.

The design also addresses: record-vs-reference namespacing, unified query/assertion relations with verb keywords, Clojure-style namespace imports, an `@EditorID` / `:keyword` syntax distinction, a flat mmap'd `.mora.patch` container, and a constexpr-driven metaprogramming source of truth that drives parsing, type checking, runtime dispatch, docs, and validation.

LLVM is removed; the runtime is a small interpreter of an operator DAG.

## Non-goals (for this spec)

- Concrete bytecode byte-layout details beyond the high-level section structure.
- The final shipping set of relations — only the framework and the initial reference set are specified.
- Persistent runtime state across save/load (flagged as a future extension: serialize the `MaintainSink` binding map on save, replay on load).

---

## Section 1 — Architecture Overview

Mora is two datalog programs sharing one source:

```
.mora source files
      │
      ▼
Compiler (partial evaluator)
  - Parses rules
  - Phase-classifies each rule (static / maintain / on)
  - Evaluates static rules now
  - Compiles dynamic rules to operator-graph bytecode
      │
      ▼
.mora.patch (flat mmap'd file, uncompressed)
  - Static patch entries
  - Pre-resolved arrangements
  - Operator-DAG bytecode
  - Keyword intern table
  - String table
      │
      ▼
MoraRuntime.dll (SKSE plugin)
  - mmap .mora.patch
  - Apply static patches on load
  - Register SKSE hooks
  - Run differential dataflow engine on event deltas
```

### Three rule tiers

1. **Static (default, no annotation).** All body relations come from ESP/record data. The compiler fully evaluates these, producing patch entries. Zero runtime cost beyond the initial apply at game load.

2. **`maintain` rules.** Body references runtime state (refs, player, world). Compiled to operator-DAG bytecode. The runtime engine tracks truth values differentially — when the body is satisfied, head effects apply; when it stops being satisfied, effects retract automatically. Maps to BG3-style maintained passives/auras.

3. **`on` rules.** Body references at least one `event/*` relation (edge-triggered). Compiled to the same operator-DAG bytecode, but head effects fire once on the `+1` transition and don't retract. For one-shot actions (give gold, spawn item).

The phase classifier validates annotations against the rule's dependencies:

- Unannotated rule using only `form/*` → OK (static).
- Unannotated rule using any dynamic relation → compile error ("did you mean `maintain` or `on`?").
- `maintain` rule using `event/*` relations → compile error (events are edge-shaped, not state).
- `maintain` rule whose head uses an effect without a retract handler → compile error.

### Why the split works

- Static rules get the full DuckDB-style columnar treatment during compilation, then produce the fastest possible runtime (just memory writes, as today).
- `maintain` rules get truth maintenance for free from differential dataflow, solving the "apply buff while conditions hold, retract when they don't" pattern without modder-written removal logic.
- `on` rules get event-delta semantics without the state-tracking overhead of maintenance.
- All three share a single syntax, a single type system, and a single constexpr-driven source of truth.

---

## Section 2 — Language Surface

### File structure

```mora
namespace my_mod.city_rewards

requires mod("Skyrim.esm")
requires mod("Dawnguard.esm")

use form :as f
use ref :refer [keyword, in_combat]
use event :refer [entered_location]
use player :as p :refer [gold]
```

- `namespace` — unique identifier for this module.
- `requires mod(...)` — declared plugin dependencies.
- `use` — Clojure-style namespace imports (new). `:as` for aliases, `:refer [...]` for direct-name imports. Unaliased full names always work; `use` is purely additive.

**Conflict handling:** if two `:refer`'d names collide across namespaces, the compiler errors at the `use` declaration with a clear disambiguation message.

**No prelude.** Every namespace use must be explicit.

### Identifiers and literals

| Syntax | Meaning | Resolved |
|--------|---------|----------|
| `@IronSword` | EditorID → FormID | Compile-time (from ESP EDID records) |
| `:low`, `:fire` | Keyword (interned symbol) | Compile-time intern, runtime is an int |
| `42`, `1.5` | Integer / Float literal | Literal |
| `"Nazeem"` | String literal | Literal |
| `Player`, `NPC` | Variable (capitalized identifier) | Bound at evaluation |
| `form/keyword` | Namespaced relation/effect | Looked up in constexpr table |

Keywords are a lightweight tag type for user-defined semantics — they're just values (interned small ints), completely separate from the FormID world.

### Rule forms

```mora
# Static (default) — compile-time, goes into static patch table
iron_weapons(W):
    form/weapon(W)
    form/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)
    => add form/keyword(W, @Enchanted)

# Maintained — runtime differential, effects retract when body fails
maintain dangerous_ref(R):
    ref/is_npc(R)
    ref/base_form(R, Base)
    form/faction(Base, @BanditFaction)
    => add ref/keyword(R, @ThreatMarker)

# Edge-triggered — fires once on +1 transition
on city_welcome(P):
    event/entered_location(P, Loc)
    form/keyword(Loc, @LocTypeCity)
    => add player/gold(P, 100)
```

### Head grammar (effect clauses)

```
=> <verb> <namespace>/<name>(<args>)
```

Verbs: `set`, `add`, `sub`, `remove`. Legal verbs per relation are determined by the relation's value type and cardinality in the constexpr table:

| Value type × cardinality | Legal verbs |
|--------------------------|-------------|
| Float, scalar | `set` |
| Int, scalar (countable) | `set`, `add`, `sub` |
| Any, set-valued | `add`, `remove` |
| Any, functional (1:1) | read-only |

Verb/shape mismatches are compile errors pointing at the source location, derived from the constexpr shape metadata.

### Body grammar

- `ns/rel(args)` — positive pattern
- `not ns/rel(args)` — negation
- `Expr op Expr` — comparison (`>`, `<`, `>=`, `<=`, `==`, `!=`)
- `Var in [val, val, ...]` — set membership
- `or: branch1; branch2;` — disjunction

### Unified query/assertion semantics

The **same relation name** serves as a query in the body and as an assertion in the head. `form/keyword(W, @Iron)` in the body queries; after `=> add` it asserts. The verb distinguishes the kind of assertion. This collapses the prior "has_keyword / add_keyword / remove_keyword" trio into a single `form/keyword` relation.

### User-defined rules with keywords

```mora
threat_level(NPC, :high):
    form/npc(NPC)
    form/base_level(NPC, L)
    L >= 30

threat_level(NPC, :low):
    form/npc(NPC)
    form/base_level(NPC, L)
    L < 10
```

User-defined rules live in the file's declared `namespace` and are importable from other files via the same `use ... :as ... :refer [...]` mechanics.

---

## Section 3 — Namespace Inventory

Five built-in namespaces with well-defined roles.

### `form/*` — Static record data

Operates on base records. Everything is known at compile time from ESP data. Body queries hit pre-evaluated static tables; head assertions emit static patch entries applied once at game load.

Examples: `form/npc`, `form/weapon`, `form/keyword`, `form/faction`, `form/damage`, `form/gold_value`, `form/name`, `form/base_level`, `form/race`.

**Source:** ESP parsing at compile time. Each entry has a constexpr `EspSource` mapping it to a record/subrecord.

### `ref/*` — Dynamic reference data

Operates on placed references (live instances). Subject is a RefID. Body queries read live game state via CommonLib or SKSE hooks; head effects mutate live references.

Examples: `ref/is_npc`, `ref/base_form` (the static↔dynamic bridge), `ref/keyword`, `ref/in_combat`, `ref/current_location`, `ref/equipped`, `ref/health`.

**Source:** SKSE event hooks for state changes, CommonLib memory reads for on-demand queries. Each entry declares a `HookSource`, `MemoryRead`, or `HandlerRead` binding.

### `player/*` — Player-specific state

Specialization of `ref/*` for the player character, with relations only the player has.

Examples: `player/gold` (countable scalar), `player/level`, `player/inventory_count`, `player/notification`.

### `world/*` — Global game state

Global singletons — relations without a subject argument.

Examples: `world/time_of_day`, `world/day_of_week`, `world/weather`, `world/difficulty`.

### `event/*` — Edge-triggered inputs

Relations existing only at the moment of an event. Consumed exclusively by `on` rules. Using an `event/*` relation in a `maintain` rule is a compile error.

Examples: `event/entered_location`, `event/took_damage`, `event/equipped_item`, `event/quest_stage_changed`, `event/killed`.

Events often pair with a corresponding state relation in `ref/*` or `player/*`; the runtime typically derives the state by maintaining it from the event.

### Namespace interaction rules

- **Bridges are explicit.** The only way from dynamic to static is through a bridge like `ref/base_form(R, F)`. The type checker rejects `form/keyword(R, ...)` when `R` is a RefId.
- **No implicit ref→form fallthrough.** `ref/keyword(R, @X)` does not include the base form's keywords. Users must join through `base_form` explicitly. Verbose but unambiguous.
- **Static-only rules stay static.** A rule using only `form/*` body relations is static. Any dynamic relation in the body promotes the rule to dynamic, requiring `maintain` or `on`.

### Extensibility

Initial shipping set is conservative — enough for SPID/KID/SkyPatcher-style use cases plus common dynamic patterns (buffs, location-based effects, kill rewards). New relations are added via the constexpr table (see Section 6).

---

## Section 4 — Runtime Engine

Small embedded differential dataflow engine, fed by SKSE hooks, joining against memory-mapped static arrangements.

### Architecture

```
SKSE hooks  →  Input deltas (event/* and derived ref/* state)
             →  Operator DAG (Filter, Project, Map, Join, SemiJoin,
                              Antijoin, StaticProbe, Distinct, Reduce)
             →  Probes against static arrangements (mmap'd, read-only)
             →  Effect sinks (MaintainSink / OnSink)
             →  Handler dispatch (from constexpr handler registry)
```

### Delta model

All state is represented as `(tuple, diff)` pairs where `diff ∈ {+1, −1}`. No timestamps in the initial design — a single logical "now" clock per event batch. Each hook firing produces deltas; the engine propagates to quiescence before returning control.

Example — player enters Whiterun:
```
(event/entered_location(@Player, @Whiterun), +1)
(ref/current_location(@Player, @Whiterun),   +1)
(ref/current_location(@Player, @PrevLoc),    −1)
```

### Operators

All deserialized from the .mora.patch DAG bytecode section:

- **Filter** — stateless predicate
- **Project** — stateless column selection
- **Map** — stateless expression (comparison, arithmetic, keyword equality)
- **Join** — stateful, arrangement on each side, emits product on delta
- **SemiJoin / Antijoin** — stateful, presence/absence against an arrangement
- **StaticProbe** — joins against a mmap'd static arrangement (zero runtime build cost); the compile-time/runtime bridge
- **Distinct / Reduce** — stateful aggregates
- **MaintainSink** — terminal; maintains a per-binding effect handle for auto-retraction
- **OnSink** — terminal; fires handler only on `+1`, discards retractions

### Arrangements

**Static arrangements** — built at compile time, stored in `.mora.patch`, mmap'd at runtime. Sorted key array + value payload, binary-searchable. Used by `StaticProbe`.

**Dynamic arrangements** — built at runtime, in DRAM (small hash maps). Used by `Join`, `Distinct`, stateful operators.

Static arrangements dominate because most Mora rules have one dynamic input probing against static qualifiers. These are effectively O(1) lookups into mmap'd memory with zero warm-up.

### Effect sinks

**`OnSink`** — receives a delta, checks `diff == +1`, calls the handler. Retractions discarded.

**`MaintainSink`** — tracks `map<(rule_id, binding_tuple), EffectHandle>`. On `+1` calls `apply_handler`, stores returned handle. On `−1` retrieves handle, calls `retract_handler`. This is how BG3-style auto-retraction works — the engine owns the lifetime.

The constexpr relation table declares each effect's `apply_handler` and optional `retract_handler`. A null retract handler means the effect is not maintainable; using it in a `maintain` rule is a compile error.

### Event delivery loop

On each SKSE hook:
1. Translate native hook arguments into input deltas.
2. Push deltas into source operators of the DAG.
3. Propagate through topologically-sorted operators until the delta queue drains.
4. Execute pending effect sink calls.

Single-threaded. SKSE hooks marshal to the game's main thread, so no synchronization. Hook handlers should be short; significant work is queued rather than blocking in the sink.

### Memory and lifetime

- Static arrangements: forever (mmap'd, read-only).
- Dynamic arrangements: grow/shrink with the game; worst-case bounded by loaded refs.
- MaintainSink state: bounded by currently-true rule bindings. In practice hundreds, not millions.
- Save/load: engine state rebuilt from scratch by replaying relevant hooks (ref loads, location, equipped items). No persistent state in the save file.

### Future extension: persistent state

Serialize the MaintainSink binding map on save; on load, re-invoke apply handlers with stored arguments to rehydrate. Not in the initial design but the architecture admits it cleanly.

---

## Section 5 — `.mora.patch` Binary Format

A single flat file, uncompressed, mmap-able in one call. Zero parsing cost — everything is offset-based pointer arithmetic.

### Top-level structure

```
Header (fixed 64 bytes)
Section Directory (N × 16 bytes)
Section 1: String Table
Section 2: Keyword Intern Table
Section 3: Static Patch Entries
Section 4: Static Arrangements
Section 5: Operator DAG Bytecode
Section 6: Effect Handler References
Section 7: Dependency Manifest
```

### Header (64 bytes)

```
magic         uint32    'MORA' (0x41524F4D)
version       uint32    file-format version
flags         uint32    reserved
section_count uint32    number of sections
file_size     uint64    total file size
toolchain_id  uint64    compiler version hash
esp_digest    [32]byte  digest of load order + plugin file hashes
```

`esp_digest` lets the runtime detect a stale `.mora.patch` quickly and fail loudly instead of applying outdated patches.

### Section Directory (16 bytes per entry)

```
section_id    uint32    enum (STRING_TABLE, KEYWORDS, PATCHES, ...)
flags         uint32    alignment class, etc.
offset        uint64    byte offset from file start
size          uint64    byte length
```

All sections are naturally aligned so mmap'd access needs no copying.

### Section layouts

**String Table** — flat bytes, strings referenced by `(offset, uint16 length)`. Deduplicated at compile time.

**Keyword Intern Table** — sorted array of `(string_offset, uint32 id)` pairs; lets the runtime recover strings for diagnostics.

**Static Patch Entries** — existing 16-byte binary format, sorted by FormID. Moved into a section.

**Static Arrangements** — pre-indexed, sorted tables the runtime queries without building anything:

```
header:
  relation_id   uint32    which relation this indexes
  key_column    uint8     which column is the join key
  row_count     uint32
  row_stride    uint16    bytes per row
  key_type      uint8     U32 / U64 / KeywordId

body:
  sorted rows, fixed stride, binary-searchable by key column
```

Common arrangements emitted for a typical compile: `form/keyword` indexed by form, `form/keyword` indexed by keyword, `form/faction` both ways, `form/base_level` by form. Only arrangements referenced by dynamic rules are emitted.

**Operator DAG Bytecode** — dynamic rules as a typed node graph:

```
header:
  node_count    uint32
  root_count    uint32    input nodes (hooks, sources)
  sink_count    uint32    output nodes (MaintainSink, OnSink)

node array:
  node_id       uint32
  opcode        uint16    Filter, Project, Join, StaticProbe, ...
  param_count   uint16
  param_offset  uint32    into parameter blob
  input_count   uint16
  input_offset  uint32    into input-edges blob

parameter blob:
  typed per-node parameters (column indices, constants, arrangement refs, handler ids)

input-edges blob:
  for each node, upstream node_ids feeding it
```

Topological order preserved by node ordering; the runtime trusts the compiler's ordering.

**Effect Handler References** — for each handler opcode the DAG references, records `HandlerId` + arg-packing schema. Validated against the runtime's handler registry at load time.

**Dependency Manifest** — plugin names, versions, file-size digests. Feeds `esp_digest` and produces helpful error messages when load order drifts.

### Runtime load path

```
1. open(mora_patches.bin)
2. mmap(PROT_READ, MAP_PRIVATE)
3. verify magic, version
4. verify esp_digest matches current load order
5. walk section directory → build section pointer table
6. validate handler refs against registry
7. apply static patches (existing fast path)
8. construct operator-DAG runtime view (pointer table over mmap'd bytecode)
9. register SKSE hooks that feed input nodes
```

No `malloc`, no string parsing, no JSON. Every "deserialize" is `base + offset`.

### File size expectations

For a large load order (Requiem-scale): string table 1–5 MB, static patches ~40 MB (2.5M × 16 bytes), arrangements 5–20 MB, DAG bytecode tens of KB. **Total: ~50–70 MB**, uncompressed. Uncompressed is a conscious choice — decompression latency on load would exceed disk read time, and the OS page cache handles cold-start efficiently.

---

## Section 6 — Metaprogramming & Build-Time Validation

One constexpr table drives parsing, type checking, evaluation, runtime dispatch, docs, IDE support, and cross-component validation.

### The master table

```cpp
// include/mora/model/relations.h
struct RelationEntry {
    std::string_view namespace_;
    std::string_view name;
    ArgSpec          args[MAX_ARGS];
    uint8_t          arg_count;
    ValueType        value_type;       // Int / Float / String / FormID / Keyword
    Cardinality      cardinality;      // Scalar / Countable / Set / Functional
    RelationSource   source;           // Static / Hook / MemoryRead / Handler / Event
    SourceBinding    binding;          // tagged union by source
    HandlerId        apply_handler;    // None if generic applier
    HandlerId        retract_handler;  // None if not retractable
    EspSource        esp_source;       // only for Static
    HookSpec         hook;             // only for Hook / Event
    std::string_view docs;
};

inline constexpr RelationEntry kRelations[] = {
    {"form", "keyword", ..., .cardinality = Set,
        .source = Static,
        .esp_source = {RecordType::Any, SubrecordTag::KWDA},
        .apply_handler = HandlerId::FormAddKeyword,
        .retract_handler = HandlerId::FormRemoveKeyword,
        .docs = "Keyword membership on a base record."},

    {"ref", "in_combat", ..., .cardinality = Functional,
        .source = MemoryRead,
        .binding = MemoryRead{offsetof(RE::Actor, combatState), ValueType::Int},
        .docs = "Whether this reference is currently in combat."},

    {"event", "entered_location", ..., .source = Event,
        .hook = HookSpec{"SKSE::OnLocationChange", HookKind::Edge},
        .docs = "Fires when a reference enters a new location."},
    // ...
};
```

### What the table drives

| Consumer | How it uses the table |
|----------|----------------------|
| Parser | Validates `namespace/name` exists; arity check |
| Type checker | Signature + verb legality + maintain/trigger legality |
| Phase classifier | `source` → static vs dynamic classification |
| Static evaluator | `esp_source` → tells ESP reader which records/subrecords to emit |
| Operator compiler | Handlers and offsets → sink emission |
| Runtime dispatch | `HandlerId` → index into handler function pointer table |
| Docs generator | `docs` + signature → markdown sections |
| IDE/LSP | Full table → completion, hover, go-to-source |

One edit adds a relation everywhere. Forget something and the build fails.

### Build-time validation

`static_assert` chains cross-check the table against parallel registries:

```cpp
static_assert(validate_handlers(kRelations, kHandlers));
static_assert(validate_esp_sources(kRelations));
static_assert(validate_memory_reads(kRelations));
static_assert(validate_hooks(kRelations, kKnownHooks));
static_assert(validate_verb_shapes(kRelations));
static_assert(validate_maintain_retractability(kRelations));
static_assert(validate_no_duplicates(kRelations));
```

**Error messages** include the offending entry via a templated helper:

```cpp
template <RelationEntry E>
struct MissingHandlerFor {
    static_assert(sizeof(E) == 0,
        "Relation has apply_handler set but no handler is registered. "
        "See the RelationEntry value shown in this instantiation.");
};
```

The compiler prints the full `RelationEntry` literal in the error, pointing readers at the exact bad row.

### CommonLib offset verifier

The compiler runs on Linux and has no CommonLib dependency. A small `.cpp` file compiled only in the runtime build asserts every `MemoryRead` offset against the actual `offsetof`:

```cpp
// src/rt/offset_verifier.cpp (runtime build only)
#include "mora/model/relations.h"
#include <RE/Skyrim.h>

static_assert(offsetof(RE::Actor, combatState) ==
              kRelations[REL_REF_IN_COMBAT].binding.memory.offset);
// ... one line per MemoryRead relation
```

If CommonLib drifts, the runtime build fails. The compiler never sees `RE::*` — it just reads constants from the table. The verifier exists purely to keep the table truthful. An optional tool could generate these lines automatically later; not initially needed.

### Docs generation

A build step walks `kRelations`, `kHandlers`, `kKnownHooks` and emits:

- `docs/src/language-reference.md` — grouped by namespace, one section per relation, with signature, verb table, docs text, source kind.
- `docs/src/relations-index.md` — alphabetical index with anchor links.
- JSON schema for IDE/LSP consumption.

Docs regenerate on every build. Empty docs strings trigger a soft-warning check.

### "Add a new relation" workflow

1. Add a `RelationEntry` to `kRelations` with full metadata.
2. Provide the required backing: register a handler + write the C++ function (if needed), ensure the ESP source is parsed, or add a memory offset plus a verifier line.
3. `xmake build` — refuses to build if anything is missing or inconsistent.
4. Commit. Docs, parser support, type checker rules, and IDE completion are live automatically.

This is the point of the metaprogramming investment: the additive case is trivial; the invalid case is impossible.

---

## Summary

Six interlocking pieces:

1. **Architecture** — static + `maintain` + `on`, one source, two datalog programs.
2. **Language surface** — syntax, `@EditorID` / `:keyword`, verbs (`set`/`add`/`sub`/`remove`), Clojure-style `use`.
3. **Namespace inventory** — `form/`, `ref/`, `player/`, `world/`, `event/`, explicit bridges, no implicit ref→form fallthrough.
4. **Runtime engine** — differential dataflow, operator DAG, mmap'd arrangements, MaintainSink/OnSink, single-threaded, stateless across save/load (with a clear future-extension path).
5. **.mora.patch format** — flat mmap'd file, typed sections, pre-indexed arrangements, handler-reference validation.
6. **Metaprogramming & validation** — constexpr master table, `static_assert` chains, CommonLib offset verifier, auto-generated docs.

Implementation ordering will be covered by the forthcoming plan document.
