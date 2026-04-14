# How Mora Works

Mora is two datalog programs sharing one source file. The compiler
partial-evaluates everything it can at build time and emits a flat binary
patch file; the runtime mmaps that file, applies the static patches, and
runs a small differential-dataflow engine for whatever couldn't be resolved
ahead of time.

This document covers the architecture at a high level. The full design
spec — operator set, binary layout down to the byte, phase-classifier
rules — is at
`docs/superpowers/specs/2026-04-14-mora-v2-static-dynamic-datalog-design.md`.
For the language surface, see [language-guide.md](language-guide.md).

---

## The three-tier rule model

Every Mora rule lands in exactly one of three tiers, decided by the
compiler from the rule's body dependencies and any explicit annotation.

### 1. Static (default, no annotation)

All body relations are in namespaces whose data is fully known at compile
time — `form/*` today. The compiler evaluates the rule against the
extracted FactDB and emits a patch entry per match. Zero runtime cost
beyond the initial memory write.

```mora
# Static: body is pure form/*, so fully evaluated at compile time.
iron_weapons(W):
    form/weapon(W)
    form/keyword(W, @WeapMaterialIron)
    => set form/damage(W, 20)
```

### 2. `maintain` — differential truth maintenance

Body touches live runtime state (typically `ref/*`, `player/*`, `world/*`).
The compiler lowers the rule to operator-DAG bytecode. At runtime the
engine tracks every binding's truth value; effects apply on the `+1`
transition and **automatically retract** on the `-1`. No modder-written
"cleanup" logic — the engine owns the lifetime.

```mora
# Maintain: add a threat-marker keyword to any ref whose base is a bandit.
# When ref/base_form stops holding (the ref is unloaded) the keyword is
# automatically removed.
maintain threat_marker(R):
    ref/is_npc(R)
    ref/base_form(R, Base)
    form/faction(Base, @BanditFaction)
    => add ref/keyword(R, @ThreatMarker)
```

A `maintain` rule whose head uses an effect without a retract handler is a
compile error.

### 3. `on` — edge-triggered

Body references at least one `event/*` relation. Same operator-DAG
bytecode, but effects fire once on `+1` and don't retract. Right for
one-shot actions: give gold, show a notification, spawn an item.

```mora
on bandit_bounty(Player, Victim):
    event/killed(Victim, Player)
    ref/is_player(Player)
    ref/is_npc(Victim)
    ref/base_form(Victim, Base)
    form/faction(Base, @BanditFaction)
    ref/level(Victim, VL)
    => add player/gold(Player, 10 * VL)
```

Mixing: `event/*` relations in a `maintain` rule is a compile error;
unannotated rules using any dynamic relation are a compile error asking
"did you mean `maintain` or `on`?".

---

## The five namespaces

| Namespace  | Subject            | Source                               |
|------------|--------------------|--------------------------------------|
| `form/*`   | Base records       | Static ESP extraction at compile time |
| `ref/*`    | Placed refs        | CommonLib reads + SKSE event hooks    |
| `player/*` | Player character   | Player-specific handlers              |
| `world/*`  | Global singletons  | CommonLib reads / hooks               |
| `event/*`  | Edge events        | SKSE hook + handler                   |

The only way from a live reference to its base record is the explicit
bridge `ref/base_form(R, F)`. No implicit ref→form fallthrough:
`ref/keyword(R, @X)` does **not** see the base form's keywords. Join
through `base_form` when you need both.

The canonical list — 58 relations at the time of writing — is in
[relations.md](relations.md), auto-generated from
`data/relations/**/*.yaml`.

---

## The metaprogramming core

One constexpr table drives everything:

- Parser: validates `namespace/name` exists with the right arity.
- Type checker: verb legality (set on a scalar, add/remove on a list, etc.),
  argument types, maintain-retractability.
- Phase classifier: static vs. dynamic vs. event based on each relation's
  declared source kind.
- Static evaluator: ESP extraction config (record type, subrecord, offsets)
  per `form/*` entry.
- Runtime: handler IDs index a small function-pointer table.
- Docs generator: `mora docs` walks the same table to emit
  `language-reference.md` and `relations.md`.

Relations are declared in YAML under `data/relations/`; `tools/gen_docs.py`
regenerates `docs/src/relations.md`, and the C++ table is the single source
of truth for everything else. A `static_assert` chain cross-checks the table
against the handler registry, ESP schema, and memory-offset verifier. Forget
a handler and the build fails.

Adding a new relation is a one-line YAML change plus whatever handler glue
it needs — the additive case is trivial; the invalid case is rejected at
build time.

---

## The compile pipeline

```
  .mora source files
        |
        v
  Parser ---- diag, types ---> Resolver ---> Type checker
        |
        v
  Phase classifier
        |
        +------- static rules --------+
        |                              |
        v                              v
  Static Datalog evaluator       Dynamic rule compiler
  (vs FactDB from ESP load)      (operator-DAG bytecode)
        |                              |
        +---------------+--------------+
                        |
                        v
                mora_patches.bin
                (header + sections)
```

### Static path

`mora compile`:

1. Parses every `.mora` file.
2. Reads the ESP load order (via `--data-dir`), extracts just the relations
   the rules actually reference — lazy fact loading, not a full DB scan.
3. Resolves `@EditorID` references to FormIDs.
4. Type checks: argument types, verb legality, phase constraints.
5. Evaluates static rules bottom-up against the FactDB.
6. Conflict-resolves (load-order aware) and deduplicates the patch set.
7. Emits `mora_patches.bin`.

### Dynamic path

Rules annotated `maintain` or `on` are lowered to an operator DAG and
serialized into the `DagBytecode` section of the same patch file. The DAG
is a typed node graph over stateless operators (`Filter`, `Project`,
`Map`), stateful operators (`Join`, `SemiJoin`, `Antijoin`, `Distinct`,
`Reduce`), and terminal sinks (`MaintainSink` for auto-retract,
`OnSink` for edge-triggered). `StaticProbe` nodes join dynamic streams
against mmap'd static arrangements with no warm-up cost.

---

## The `.mora.patch` binary format

One flat, uncompressed, mmap-in-one-call file. Every "deserialize" is
pointer arithmetic against a base address.

```
Header (64 bytes)
Section Directory (N x 16 bytes)
  -> String Table
  -> Keyword Intern Table
  -> Static Patch Entries    (16 bytes each, sorted by FormID)
  -> Static Arrangements     (pre-indexed, binary-searchable)
  -> Operator DAG Bytecode   (dynamic rules)
  -> Effect Handler Refs
  -> Dependency Manifest     (plugin list + digest)
```

The header carries an `esp_digest` covering the load order and plugin file
hashes. On startup the runtime compares against the live load order and
fails loudly instead of applying stale patches.

Uncompressed is a conscious choice: decompression latency on load would
exceed disk read time, and the OS page cache handles cold start
efficiently. A full Requiem-scale compile lands around 50–70 MB.

For the byte-level layout see the
[v2 design spec](https://github.com/halgari/mora/blob/master/docs/superpowers/specs/2026-04-14-mora-v2-static-dynamic-datalog-design.md#section-5-morapatch-binary-format).

---

## The runtime

`MoraRuntime.dll` is a small SKSE plugin. On load it:

1. `mmap`s `mora_patches.bin` read-only.
2. Verifies the magic, version, and `esp_digest`.
3. Applies the static patch section — a tight loop of memory writes at
   Address-Library-resolved offsets.
4. Walks the `DagBytecode` section and **opt-in** registers only the SKSE
   hooks and `ref/*` readers that the loaded rules actually reference.
   A rule set that never uses `event/killed` never installs the kill hook.
5. Returns control to SKSE.

On each relevant SKSE event, the runtime translates native arguments into
input deltas (`(tuple, +1)` or `(tuple, -1)`), pushes them into the DAG,
propagates to quiescence, and invokes effect sinks.

Threading model: single-threaded. SKSE hooks marshal to the game's main
thread, so no synchronization is needed. Hook handlers should be short;
significant work is queued rather than blocking in the sink.

### Lifetime

- Static patches: applied once at game load, read-only for the session.
- Static arrangements: stay mmap'd for the life of the process.
- Dynamic operator state: in DRAM, bounded by live refs and rule bindings.
- Save/load: maintain state is **not** currently persisted across saves.
  The engine rebuilds by replaying relevant hooks on load. Save-state
  serialization is flagged as a future extension in the design spec.

---

## The key shift

Every existing Skyrim patcher (SPID, KID, SkyPatcher, …) re-evaluates its
rules at every game launch against data that doesn't change between
launches. Mora evaluates once on your build machine, against your exact
load order, and ships just the answers.

Anything that can't be known until runtime is still expressible — via
`maintain` and `on` — but it runs on the same operator primitives, shares
the same type system, and joins cheaply against the same mmap'd static
tables. The split isn't "fast stuff vs. slow stuff"; it's "what's a pure
function of the ESPs vs. what needs live state", which is a real property
of the rule, not a cost decision.

Further reading:

- [Language Guide](language-guide.md) for writing rules.
- [Relation Reference](relations.md) for the namespace inventory.
- The [v2 design spec](https://github.com/halgari/mora/blob/master/docs/superpowers/specs/2026-04-14-mora-v2-static-dynamic-datalog-design.md)
  for operator-by-operator detail.
