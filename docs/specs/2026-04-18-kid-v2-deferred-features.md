# KID v2: Deferred Features

> **Status:** Partially superseded. Waves 1 (FormID-ref), 2 (AND-of-ORs),
> 3 (wildcards) and the wildcard-in-AND-group restriction were all
> resolved by the 2026-04-19 rule-synthesis refactor — KID lines now
> compile directly to Mora `Rule` AST nodes (see
> `extensions/skyrim_compile/include/mora_skyrim_compile/kid_compiler.h`),
> so AND-semantics, wildcard cross-product expansion, and FormID
> resolution are all naturally expressible in the rule body. Remaining
> work: extend `kid_rule_builder.cpp` to emit conjuncts for the
> additional traits documented in waves 2-3 below (HEAVY/LIGHT/
> CLOTHING, AR/W ranges, body slots, spell/magic-effect flags). Each
> trait is now a small localised edit (one new helper, one extra
> conjunct) rather than the per-type variant explosion v1's stdlib
> required.

## Background

KID v1 ingested `*_KID.ini` as facts (`ini/kid_*`) and wired them into
`skyrim/add(X, :Keyword, KW)` via a bundled stdlib
(`data/stdlib/kid.mora`). Both the facts and the stdlib were removed
on 2026-04-19; lines are now compiled to rules instead. See
`docs/src/kid-integration.md` for the user-facing view.

The original v1 limitations — flattening `+` (AND) to `,` (OR),
rejecting FormID refs, dropping wildcards inside AND-groups, and
parsing-but-ignoring trait filters beyond `E` / `-E` — are now mostly
gone. The four subsections below were the original v2 plan; they
remain useful as a reference for the trait wiring still to come.

---

## 1. FormID-ref resolution (`0xFFF~Mod.esp`)

### Problem

KID accepts two identifier encodings:

| Form | Example | v1 |
|---|---|---|
| EditorID | `MyKeyword` | ✅ resolved |
| FormID + plugin | `0x12AB~MyMod.esp` (ESP/ESM) | ❌ `kid-formid-unsupported`, line dropped |
| FormID + plugin | `0xFFF~MyMod.esl` (ESL / light) | ❌ dropped |

KID's own docs recommend EditorIDs, but real KID files in the wild
mix both, especially when a plugin author wants to reference a form
from another mod whose EditorID might change between releases.

### Required context for resolution

Given `0xNNNN~Plugin.ext`, the final runtime FormID is:

- **ESP/ESM**: `(runtime_index << 24) | local_id`
  - `runtime_index` is `0x00..0xFD`, the plugin's position in the
    resolved load order (not the file-system order).
- **ESL** (light plugin): `0xFE000000 | (esl_slot << 12) | (local_id & 0xFFF)`
  - `esl_slot` is `0x000..0xFFF`, the plugin's position among ESLs
    (separate counter from ESP/ESM indices).

Both encodings are already computed inside `SkyrimEspDataSource` via
`mora::RuntimeIndexMap` (`extensions/skyrim_compile/include/mora_skyrim_compile/esp/load_order.h:26`).
The map has:

```cpp
struct RuntimeIndexMap {
    std::unordered_map<std::string, uint32_t> index;
    std::unordered_set<std::string>           light;
    uint32_t globalize(uint32_t local_id, const PluginInfo& info) const;
    static RuntimeIndexMap build(const std::vector<PluginOrderEntry>& entries);
};
```

The resolver doesn't need `PluginInfo`; it only has a plugin filename
and a local id. Calling `globalize` requires a PluginInfo to look up
MAST masters, but for a **self-reference** (`0xNNN~ThisPlugin.esp`
where the `~ThisPlugin` part IS the plugin, not a master of it), the
MAST list is irrelevant — we're not following any reference chains.

### Design

Expose the minimum data needed, not the whole `RuntimeIndexMap`, to
avoid coupling the resolver to ESP internals.

**Interface additions.**

Add one field to `LoadCtx` (symmetric to the existing `editor_ids_out`):

```cpp
// include/mora/ext/data_source.h
struct LoadCtx {
    // …existing fields…

    // Plugin filename (ASCII, exact case as seen on disk) → packed
    // runtime-index descriptor. Encoding:
    //   - bit 31:     1 iff ESL / light (0xFE)
    //   - bits 30..12: unused for ESP, ESL slot (0x000..0xFFF) for ESL
    //   - bits 11..0:  ESP/ESM runtime index (0x00..0xFD), else 0
    //
    // Helper `mora::ext::globalize_formid` in runtime_index.h composes
    // a final FormID from (descriptor, local_id).
    std::unordered_map<std::string, uint32_t>* plugin_runtime_index_out = nullptr;
};
```

Reuse the `uint32_t` value slot via a packed encoding rather than
adding a second map; keeps the shape matching `editor_ids_out` and
means the resolver doesn't need to juggle two parallel lookups.

**New helper.**

```cpp
// include/mora/ext/runtime_index.h
namespace mora::ext {
// Compose the final 32-bit FormID from a descriptor (see LoadCtx
// comment) and a local record id. Returns 0 if `descriptor == 0`
// (unknown plugin) so callers can distinguish a miss.
uint32_t globalize_formid(uint32_t descriptor, uint32_t local_id);
} // namespace mora::ext
```

Implementation:

```cpp
uint32_t globalize_formid(uint32_t d, uint32_t local_id) {
    if (d == 0) return 0;
    bool is_light = (d >> 31) & 1;
    if (is_light) {
        uint32_t slot = (d >> 12) & 0xFFF;
        return 0xFE000000u | (slot << 12) | (local_id & 0xFFF);
    }
    uint32_t idx = d & 0xFF;
    return (idx << 24) | (local_id & 0x00FFFFFF);
}
```

**ESP datasource changes.**

`esp_data_source.cpp` currently only fills `editor_ids_out`. Add a
parallel loop that reads `RuntimeIndexMap::index` and `light`, packs
the descriptor, and writes to `plugin_runtime_index_out` if non-null.
The map is built once per `load()` — no new computation, just a new
output channel.

**Resolver changes.**

`kid_resolver.cpp::resolve_ref` currently returns `FormidUnsupported`
for non-EditorID refs. Replace that branch with:

```cpp
if (!ref.is_editor_id()) {
    if (!ctx.plugin_runtime_index) {
        return ResolveError::FormidUnsupported;  // caller missed the opt-in
    }
    auto it = find_case_insensitive(*ctx.plugin_runtime_index, ref.mod_file);
    if (it == ctx.plugin_runtime_index->end()) {
        return ResolveError::MissingPlugin;
    }
    out_formid = mora::ext::globalize_formid(it->second, ref.formid);
    return ResolveError::None;
}
```

Case-insensitive lookup mirrors the EditorID path: plugin filenames
drift in case between Plugins.txt and the filesystem under Wine.

**New diagnostic.**

- `kid-missing-plugin`: `0xFFF~Unknown.esp` references a plugin not in
  the resolved load order. Matches the existing `plugin-missing`
  pattern in ESP loading.

### Testing

- Unit: extend `kid_resolver_test.cpp` with a case that seeds a
  `plugin_runtime_index` map and asserts:
  - ESP self-ref resolves to `idx<<24 | local`.
  - ESL self-ref resolves to `0xFE | slot<<12 | local`.
  - Unknown plugin produces `kid-missing-plugin`.
- Unit: `globalize_formid` roundtrip — pack/unpack descriptors, verify
  the identities hold for all three encoding paths.
- Integration: add a `0x12AB~SampleMod.esp` line to
  `test_data/kid/SampleMod_KID.ini` and assert it resolves against a
  fixture ESP that declares the record with that local id.

### Out of scope

- **MAST-list resolution** — if a KID line says `0x001~Dragonborn.esm`
  from the context of an ESP that has Dragonborn in its MAST list,
  the user typically wants the local id relative to Dragonborn.esm.
  KID's own semantics here are ambiguous; v1 of this feature only
  handles self-references (`0xNNN~ThisPlugin`). A v3 follow-up could
  add `~Master.esm` lookup if a real KID file in a real loadout
  needs it.

---

## 2. Wildcards (`*Iron`, `Iron*`, `*Iron*`)

### Problem

KID filter values can include `*` wildcards:

```
MyKeyword|Weapon|*Iron|NONE|100
```

…matches every weapon whose EditorID ends with `Iron` (e.g.
`WeapIron`, `MyModIron`). v1 treats `*Iron` as a literal EditorID,
which never resolves.

KID also applies wildcards to the item's **FULL** (display) name in
some contexts; this spec focuses on EditorID matching, which is the
documented behavior and simpler to implement because the EditorID map
is already fully materialized.

### Design choice: pre-expansion vs. built-in predicate

Two approaches:

**A. Pre-expansion at the resolver.** For each wildcard filter value,
walk the EditorID map and emit one `ini/kid_filter` row per match.
`*Iron` with 50 matching EditorIDs produces 50 rows.

**B. `matches_glob` built-in.** Store `ini/kid_filter(RuleID, "keyword_glob", StringPattern)`
and add a built-in `matches_glob(String, Pattern)` predicate the
evaluator recognizes. Wiring rules call it.

Trade-offs:

| | A (pre-expand) | B (built-in) |
|---|---|---|
| Fact count | High (N matches per wildcard) | 1 row per wildcard |
| Evaluator changes | None | New built-in + planner hook |
| Runtime cost | Same — bulk join is indexed either way | Same |
| Debugging | Patches cleanly attributable to individual keywords | Attribution needs reverse lookup |
| Fact DB size | Can balloon for `*` (matches everything) | Tiny |

Pick **A** for v2. It's strictly data-layer work, reuses the existing
bulk-join machinery, and doesn't need any evaluator surgery.
The `*` (match-everything) degenerate case is a footgun but rare in
practice; detect it at resolve time and emit a warning rather than
expanding to the full EditorID set.

### Schema changes

None. The emitted facts are already plain `ini/kid_filter(RuleID, "keyword", FormID)`
rows — pre-expansion just produces more of them.

### Parser changes

Currently the parser stores each filter value verbatim as a `KidRef`.
Extend `KidRef` with a `wildcard` flag and preserve the pattern string
on the `editor_id` slot:

```cpp
struct KidRef {
    std::string editor_id;      // or glob pattern when wildcard==true
    uint32_t    formid = 0;
    std::string mod_file;
    bool        wildcard = false;  // NEW
};
```

The parser sets `wildcard = true` when the token contains `*` (and
isn't a FormID). No other parser changes — the token is stored as-is,
the resolver does the matching.

### Resolver changes

New helper:

```cpp
// Returns every FormID whose EditorID matches `pattern`. Pattern
// supports '*' (zero or more chars) and '?' (exactly one char). Case
// insensitive (as KID itself does). Degenerate pattern "*" is rejected
// with a diagnostic — users get the same effect by omitting the filter.
std::vector<uint32_t> expand_glob(std::string_view pattern,
                                   const std::unordered_map<std::string, uint32_t>& editor_ids);
```

Resolver flow for a wildcard filter value:

```
for (ref in filter_group.values):
    if ref.wildcard:
        matches = expand_glob(ref.editor_id, editor_ids);
        if matches.empty():
            warning("kid-wildcard-empty", "no EditorIDs match '<pat>' — line keeps non-wildcard filters")
            continue;
        if matches.size() > WILDCARD_FANOUT_WARN (e.g. 1000):
            warning("kid-wildcard-fanout", "wildcard expanded to <N> rows — consider narrowing")
        filter_values.extend(matches)
    else:
        // existing EditorID path
```

### Wiring

No stdlib changes — rules still join `ini/kid_filter(RuleID, "keyword", KW)` against `form/keyword(X, KW)`. 

### Testing

- Unit: `expand_glob` on a synthetic map — star, prefix, suffix,
  middle, multiple stars, `?`, case variants.
- Unit: resolver with a wildcard filter value → N rows, each with the
  correct FormID.
- Unit: degenerate `*` pattern produces `kid-wildcard-all` diagnostic
  and drops that filter value.
- Unit: no-match wildcard produces `kid-wildcard-empty` and drops the
  value but keeps the rest of the line.
- Fixture: add a `*Iron|Weapon|*IronKeyword|NONE|100` line to
  `SampleMod_KID.ini` and assert the expected number of `skyrim/add`
  rows in the end-to-end parquet output.

### Out of scope

- **FULL name (display name) wildcards** — `*Sword of Fire*` against
  item display names. Requires `form/name(F, S)` (exists) plus a new
  `kid_filter(RuleID, "name_glob", Pattern)` kind or a second
  pre-expansion path keyed on names. Defer until a real KID file in
  the wild demonstrates it's in use.

---

## 3. True AND-of-ORs (`+` operator)

### Problem

KID's filter syntax is an AND-of-ORs:

```
KW|Armor|ArmorHeavy+ArmorGauntlet,Iron|NONE|100
     │    └───────────────┬──────────┘
     │             ( A AND B ) OR C
```

v1 flattens to `A OR B OR C` (each value becomes its own
`ini/kid_filter` row). This over-matches: any heavy armor OR any
gauntlet OR any iron item gets tagged, rather than only items that
are both heavy AND gauntlets.

Example impact: `ArmorHeavy+ArmorGauntlet` on vanilla Skyrim.esm in v1
tags ~800 armors (every heavy piece + every gauntlet) instead of ~30
(only heavy gauntlets).

### Design

Add a GroupID dimension to filter facts and express AND-within-group
via negation-as-failure (standard Datalog workaround for universal
quantification).

**Schema change.** Replace the current three-column `ini/kid_filter`
with a four-column form:

```
ini/kid_filter(RuleID: Int, GroupID: Int, Kind: String, Value: FormID)
```

Rows with the same `(RuleID, GroupID)` are AND'd; distinct GroupIDs
within the same RuleID are OR'd.

Migration: the v1 schema is a subset — every v1 line gets GroupID=0
for its single OR-group of values. The change is backwards
incompatible for anyone reading parquet dumps of `ini/kid_filter` and
breaking out columns by index; mark it in the changelog.

### Resolver changes

The KidLine parser already captures the AND-of-ORs structure as
`std::vector<KidFilterGroup>` (each group is one OR-group; `+` members
of a group are stored as a vector within the group). Today the
resolver flattens both levels into a flat list of values.

New behavior: assign a monotonic GroupID per (RuleID, or-group) and
emit one row per (or-group, and-member):

```
line: KW|Armor|A+B,C|NONE|100
RuleID = 42
Group 1 (from "A+B"):
    ini/kid_filter(42, 1, "keyword", A)
    ini/kid_filter(42, 1, "keyword", B)
Group 2 (from "C"):
    ini/kid_filter(42, 2, "keyword", C)
```

### Wiring rule changes

Current v1 rule:

```mora
skyrim/add(X, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "armor")
    form/armor(X)
    ini/kid_filter(RuleID, "keyword", KW)
    form/keyword(X, KW)
    not _kid_excluded(X, RuleID)
```

v2 rule (per item type):

```mora
# Item FAILS group G if there exists a required keyword in G that the
# item doesn't have. Standard negation-as-failure for ALL-quantifier.
_kid_group_failed(Item, RuleID, GroupID):
    ini/kid_filter(RuleID, GroupID, "keyword", KW)
    not form/keyword(Item, KW)

# Item MATCHES group G iff G exists for RuleID and the item doesn't fail it.
_kid_group_matches(Item, RuleID, GroupID):
    ini/kid_filter(RuleID, GroupID, "keyword", _)    # group-existence witness
    form/armor(Item)                                  # (one per item type)
    not _kid_group_failed(Item, RuleID, GroupID)

# Rule fires if ANY group matches.
skyrim/add(Item, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "armor")
    form/armor(Item)
    _kid_group_matches(Item, RuleID, _)
    not _kid_excluded(Item, RuleID)

# No-filter variant unchanged: fires if there's no filter at all.
skyrim/add(Item, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "armor")
    form/armor(Item)
    not ini/kid_filter(RuleID, _, _, _)
    not _kid_excluded(Item, RuleID)
```

Two new derived rules (`_kid_group_failed`, `_kid_group_matches`)
shared across all item types — so the per-item-type wiring cost stays
roughly constant, not 2x. Total rule count grows from 39 (v1) to
~59 (2 shared helpers + 19×3 per-type variants).

**Evaluator requirement.** This pattern needs nested negation: the
body of `_kid_group_matches` contains `not _kid_group_failed(...)`,
which itself is a rule whose body contains `not form/keyword(...)`.
Confirm the current vectorized `AntiJoinOp` handles two levels of
stratified negation. Commit `679ec78` suggests it does, but verify
with a targeted test before landing v2 wiring.

### Testing

- Unit: resolver emits correct GroupIDs for `A+B,C,D+E+F`:
  - Group 1: A, B
  - Group 2: C
  - Group 3: D, E, F
- Unit: stdlib rules — build a tiny FactDB with two armors (one
  heavy-gauntlet, one just heavy) + a rule `KW|Armor|Heavy+Gauntlet`
  and assert only the heavy-gauntlet armor gets the keyword.
- Regression: the SampleMod_KID.ini line `ArmorHeavy+ArmorGauntlet,...`
  should produce fewer `skyrim/add` rows than v1.

### Out of scope

- **Negation within a group**: KID doesn't allow `-Keyword` inside
  filter strings (only in traits), so this spec doesn't need to
  handle it.
- **Large group cardinality**: if a single group has >10 members,
  the evaluator does O(items × group_size) work. Acceptable for
  KID-scale inputs; revisit if a real file breaks it.

---

## 4. Trait filters

### Problem

KID's 4th field carries item-shape predicates that don't fit the
keyword-membership model. v1 stores them in `ini/kid_trait(RuleID, Trait)`
but no wiring rule consumes them — the traits are a no-op today.

KID trait vocabulary:

| Trait | Scope | Meaning |
|---|---|---|
| `E` / `-E` | weapon, armor, ammo | Has / doesn't have an enchantment |
| `T` / `-T` | weapon, armor | Uses / doesn't use a template |
| `HEAVY`, `LIGHT`, `CLOTHING` | armor | Armor type |
| `AR(min,max)` | armor | Armor rating range (float) |
| `W(min,max)` | weapon, armor | Weight range |
| `30`..`61` (body slot numbers) | armor | Body slot membership |
| `H` / `-H` | magic effect | Hostile flag |
| `D` / `CT` | magic effect, spell | Delivery / casting type filters |
| `DISPEL` | spell | Has DispelWithKeywords flag |
| `SCHOOL(min,max)` | spell | Actor-value school range |
| `AV(value)` | enchantment, scroll, potion, book, spell | Actor value taught |

Covering all of these is a large body of work. This spec groups them
by implementation cost and recommends shipping in waves.

### Wave 1: enchantment flag (`E`, `-E`)

The simplest and most commonly used traits.

**New relation:** `form/is_enchanted(F: FormID)` — a predicate that
fires for any weapon, armor, or ammo record whose EITM subrecord is
non-null.

**ESP extraction:** Add an entry to `form_model.h::kKeywordRecords`-like
list, extracted via a new `bit_test`-style rule: "record has EITM
subrecord with non-zero FormID → predicate true". The ESP reader
already scans EITM for other purposes (the `enchantment` existence
predicate only covers ENCH records, not items *with* enchantments —
different thing).

Rough sketch in form_model YAML (under `form/shared.yaml` since it
spans weapon/armor/ammo):

```yaml
is_enchanted:
  type: predicate
  args: [{name: F, type: FormRef}]
  source: static
  esp: {subrecord: EITM, extract: subrecord, read_as: formid}
  docs: "True when a weapon/armor/ammo has a non-null enchantment (EITM)."
```

The existing extractor needs a "non-zero FormID produces an existence
fact" mode — add `EspExtract::NonZeroExistence` if it doesn't exist.

**Wiring:** for each item type that supports `E`/`-E` (weapon, armor,
ammo), emit trait-aware rule variants:

```mora
# Trait: E — only enchanted items.
skyrim/add(X, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "weapon")
    form/weapon(X)
    ini/kid_trait(RuleID, "E")
    form/is_enchanted(X)
    _kid_group_matches(X, RuleID, _)
    not _kid_excluded(X, RuleID)

# Trait: -E — only non-enchanted.
skyrim/add(X, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "weapon")
    form/weapon(X)
    ini/kid_trait(RuleID, "-E")
    not form/is_enchanted(X)
    _kid_group_matches(X, RuleID, _)
    not _kid_excluded(X, RuleID)

# Existing trait-less rule needs to become trait-EXCLUSIVE:
skyrim/add(X, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "weapon")
    form/weapon(X)
    not ini/kid_trait(RuleID, _)       # NEW guard
    _kid_group_matches(X, RuleID, _)
    not _kid_excluded(X, RuleID)
```

The `not ini/kid_trait(RuleID, _)` guard on the existing variants is
load-bearing: without it a rule with `E` trait would fire both the
trait-aware variant AND the bare variant, double-applying the keyword.

Rule count: 3 weapon variants × 1 (no-filter) or 2 (filtered) = 6
instead of 2; same for armor and ammo. Net growth: +12 rules for
Wave 1.

### Wave 2: armor shape (`HEAVY`, `LIGHT`, `CLOTHING`, `AR(min,max)`, `W(min,max)`, body slots)

Armor's BODT/BOD2 subrecord encodes:
- Armor type: 0 = light, 1 = heavy, 2 = clothing (2 bits)
- Body slot mask: 32-bit flags, slots 30-61

**New relations:**

```yaml
# form/armor.yaml additions
armor_type:
  type: const<Int>
  args: [{name: A, type: FormRef}, {name: T, type: Int}]
  source: static
  esp:
    record_type: ARMO
    subrecord: BOD2
    extract: packed_field
    offset: 8                   # BOD2 armor-type field
    read_as: uint32
  docs: "Armor type: 0=light, 1=heavy, 2=clothing."

body_slot:
  type: list<Int>
  args: [{name: A, type: FormRef}, {name: SLOT, type: Int}]
  source: static
  esp:
    record_type: ARMO
    subrecord: BOD2
    extract: bit_test_list     # NEW: emits one fact per set bit
    offset: 0                  # body-slot mask field
    read_as: uint32
  docs: "Armor body slot membership (one fact per set bit, slot = 30 + bit_index)."
```

`bit_test_list` is a new extractor kind — the existing `bit_test` emits
a single predicate for one bit; we need a variant that emits multiple
list rows, one per set bit. Plumbing change in
`extensions/skyrim_compile/src/esp/esp_reader.cpp`.

`armor_rating` already exists in the schema (2-arg `form/armor_rating(A, V)`).

`weight` likewise exists for weapon/armor.

**Range trait implementation.** `AR(10,50)` and `W(5,15)` need numeric
comparison in rule bodies. The evaluator already supports guards like
`Level >= 20` (see `test_data/example.mora:29`), so this is expressible:

```mora
skyrim/add(X, :Keyword, TargetKW):
    ini/kid_dist(RuleID, TargetKW, "armor")
    form/armor(X)
    ini/kid_trait_ar_range(RuleID, Min, Max)    # new fact schema
    form/armor_rating(X, AR)
    AR >= Min
    AR <= Max
    _kid_group_matches(X, RuleID, _)
    not _kid_excluded(X, RuleID)
```

This requires a new fact flavor: range traits need a 3-arg
`ini/kid_trait_range(RuleID, Kind, Min, Max)` or kind-specific facts.
Prefer kind-specific: `ini/kid_trait_ar_range`, `ini/kid_trait_w_range`, etc. Keeps the rule patterns grep-able.

**Body-slot trait.** KID's body-slot trait `32,34` means "armor
occupies body slot 32 OR 34". Emit as multiple `ini/kid_trait_body_slot(RuleID, Slot)`
rows; wire via `form/body_slot(X, S)` join.

### Wave 3: spell / magic effect / enchantment traits

`H`, `-H`, `D`, `CT`, `DISPEL`, `SCHOOL(min,max)`, `AV(value)` — each
needs a corresponding extracted relation. These are lower priority:
the vast majority of real KID files in Wabbajack loadouts use Waves
1-2 traits; 3 is tail-of-the-distribution.

Spec pattern: for each trait, (1) identify the ESP subrecord/field,
(2) add a form/* relation, (3) extend the stdlib with a trait-aware
wiring variant per applicable item type.

Concrete example — `DISPEL` (spell has DispelWithKeywords flag):

```yaml
# form/spell.yaml
dispel_with_keywords:
  type: predicate
  args: [{name: S, type: FormRef}]
  source: static
  esp:
    record_type: SPEL
    subrecord: SPIT
    extract: bit_test
    offset: 4
    read_as: uint32
    bit: 19       # spell-data flag bit 19 = DispelWithKeywords
  docs: "Spell has the DispelWithKeywords flag."
```

Wiring rule variant for spells with `DISPEL` trait added to kid.mora,
following the `E`/`-E` pattern.

### Resolver changes across all waves

Today the resolver silently ignores unrecognized traits. Change it to
emit `ini/kid_trait_*` facts using the appropriate schema per trait.
Parse the range/value traits (`AR(10,50)`, `AV(20)`) into two or
three fact columns as appropriate.

Stub sketch:

```cpp
for (const auto& trait : line.traits) {
    if (trait == "E" || trait == "-E") {
        emit("ini/kid_trait", {rid, make_string(pool.intern(trait))});
    } else if (starts_with(trait, "AR(")) {
        auto [min, max] = parse_range(trait);  // "AR(10,50)" -> (10, 50)
        emit("ini/kid_trait_ar_range", {rid, make_int(min), make_int(max)});
    } else if (is_body_slot(trait)) {
        emit("ini/kid_trait_body_slot", {rid, make_int(slot)});
    } else if (trait == "HEAVY") {
        emit("ini/kid_trait_armor_type", {rid, make_int(1)});
    }
    // … etc
}
```

### Tradeoffs & non-obvious choices

- **Per-kind vs. unified trait relation.** Splitting `kid_trait_*` by
  kind (body_slot, ar_range, enchanted, …) over a single
  polymorphic `kid_trait(RuleID, Kind, ArgList)` trades schema
  simplicity for rule simplicity. Since each trait has a
  characteristic arity and value type, per-kind relations are cleaner
  in the wiring rules and the FactDB. Accept the schema fan-out.
- **Rule count growth.** Each trait × item-type combination adds a
  rule variant. Wave 1-2 roughly doubles the stdlib rule count. The
  evaluator has shown it handles thousands of rules fine, so this is
  not a concern at kid.mora scale.
- **Trait + filter interaction.** A line can have both a keyword
  filter AND a trait. Wiring rules must allow both to be simultaneous
  constraints, not just one-or-the-other. The `_kid_group_matches`
  join already handles the keyword side; the trait predicate
  composes with it.

### Testing

Per wave:
- Unit: resolver emits correct `ini/kid_trait_*` facts for each trait
  syntax variant.
- Unit: ESP extraction populates each new `form/*` relation against
  a minimal fixture (add to `skyrim_compile/tests/getters/`).
- Integration: stdlib rule produces expected patches for trait-filtered
  KID lines (use Skyrim.esm, assert specific FormIDs get or don't get
  keywords).

### Out of scope (even in v2)

- **Non-standard traits** found in some community KID forks.
  Upstream-only; flag with `kid-unknown-trait` warning.
- **Chance rolls** — `chance < 100` still applies unconditionally at
  compile time. Compile-time chance simulation is a different feature
  class; runtime chance rolls require KID's DLL anyway.

---

## Dependencies between features

```
FormID-ref ─┐
            ├──> both depend on LoadCtx additions but are otherwise independent.
Wildcard ───┘

AND-of-ORs ──> changes `ini/kid_filter` schema (adds GroupID).
               Wildcards (2) depend on this if wildcards inside '+'-groups
               need to stay in their group; simplest: resolve wildcards
               FIRST (inside each group), then emit group-aware filter rows.

Traits ─────> independent of 1, 2, 3. Stackable on whichever shipped.
```

Recommended ship order: **FormID-ref → AND-of-ORs → wildcards → traits**.
FormID-ref is smallest and unblocks users who hit its diagnostic
often. AND-of-ORs is a correctness fix (v1 over-matches). Wildcards
are a convenience — ship after the semantics are right. Traits is the
largest and most incremental, ship in waves.
