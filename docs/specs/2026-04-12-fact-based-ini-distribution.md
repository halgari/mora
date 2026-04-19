# Fact-Based INI Distribution

> **Status:** Historical design doc. KID landed via the
> `feat/kid-integration` branch (see `docs/src/kid-integration.md`) but
> with several deviations: ingestion is via a DataSource plugin rather
> than the `import_kid` lexer keyword (still reserved, still unused);
> filter values are stored as individual rows under
> `ini/kid_filter(RuleID, Kind, Value)` rather than list-typed columns;
> `form/*` existence predicates drive wiring instead of generic item-
> type rules. SPID is **not** integrated yet; the schema proposed here
> is still the right shape for that follow-up. Paired plan:
> `docs/plans/2026-04-12-fact-based-ini-distribution.md`.

## Problem

The current INI import system generates one named Mora rule per INI line (12,000+ rules for a typical Wabbajack loadout). Each rule goes through the full Datalog evaluation pipeline independently — clause matching, binding allocation, FactDB queries. This is slow (minutes for 12K rules) and semantically wrong (SPID OR filters were being emitted as AND clauses).

## Design

INI lines become **facts** in the FactDB instead of rules. A small fixed set of **generic distribution rules** evaluates all distributions via joins. This collapses 12,000 independent evaluations into a handful of bulk joins.

## Fact Schema

### SPID Facts

Each SPID INI line produces one `spid_dist` fact plus zero or more filter/exclude/level/metadata facts, all sharing a `RuleID` (monotonic integer assigned at parse time).

```
spid_dist(RuleID: Int, DistType: String, Target: FormID)
```

Distribution types: `"keyword"`, `"spell"`, `"perk"`, `"item"`, `"faction"`.

```
spid_filter(RuleID: Int, FilterKind: String, Values: List<FormID|String>)
```

FilterKind values:
- `"keyword"` — NPC must have any keyword in Values (`has_keyword` join)
- `"editor_id"` — NPC editor_id must match any string in Values
- `"race"` — NPC race must be any FormID in Values (`race_of` join)
- `"faction"` — NPC must be in any faction in Values (`has_faction` join)

Multiple `spid_filter` facts for the same RuleID are AND'd (all must be satisfied). Within each filter's Values list, entries are OR'd (any must match).

```
spid_exclude(RuleID: Int, FilterKind: String, Values: List<FormID|String>)
```

Same FilterKind values. An exclude is satisfied when the NPC does NOT match any value in the list. Multiple excludes are AND'd.

```
spid_level(RuleID: Int, Min: Int, Max: Int)
```

Min=0 means no lower bound. Max=0 means no upper bound.

```
spid_chance(RuleID: Int, Chance: Int)
spid_count(RuleID: Int, Count: Int)
```

### KID Facts

```
kid_dist(RuleID: Int, TargetKeyword: FormID, ItemType: String)
```

ItemType values: `"weapon"`, `"armor"`, `"ammo"`, `"potion"`, `"book"`, `"spell"`, `"misc_item"`, `"magic_effect"`, `"ingredient"`, `"activator"`, `"flora"`, `"scroll"`, `"soul_gem"`, `"location"`, `"key"`, `"furniture"`, `"enchantment"`, `"race"`.

```
kid_filter(RuleID: Int, FilterKind: String, Values: List<FormID|String>)
kid_exclude(RuleID: Int, FilterKind: String, Values: List<FormID|String>)
```

Same semantics as SPID filters.

## Value Type: List

Add `Value::Kind::List` to the existing Value tagged union. A list is a `std::vector<Value>` stored inline (small — typically 1-20 entries from INI filter fields).

The `in` operator checks membership: `Element in ListValue` returns true if any element in the list matches Element. This works with the existing `InClause` AST node but extended to accept a list-typed Value from a FactDB binding (not just literal lists in source code).

## Generic Evaluation Rules

These are internal rules registered by the compiler, not written in .mora files. They replace the 12,000 generated rules.

### SPID Keyword Distribution

```
_spid_kw_by_keyword(NPC, RuleID):
    spid_dist(RuleID, "keyword", Target)
    spid_filter(RuleID, "keyword", KWList)
    npc(NPC)
    has_keyword(NPC, KW)
    KW in KWList
    => add_keyword(NPC, Target)
```

With clause reordering: `spid_filter` (has constant "keyword") runs first, returning all SPID rules that filter by keyword. For each, the `KW in KWList` expands the list, then `has_keyword(NPC, KW)` does an indexed join on the keyword column. This is one bulk pass over all SPID keyword rules.

Similar rules for:
- `_spid_kw_by_editor_id` — joins on `editor_id(NPC, EdID)` where `EdID in EdIDList`
- `_spid_kw_by_race` — joins on `race_of(NPC, Race)` where `Race in RaceList`
- `_spid_kw_by_faction` — joins on `has_faction(NPC, Fac)` where `Fac in FacList`
- `_spid_kw_no_filter` — rules with no filters (distribute to all NPCs)

And per distribution type (spell, perk, item, faction) — same pattern, different effect.

### KID Keyword Distribution

```
_kid_weapon_kw(Item, RuleID):
    kid_dist(RuleID, KW, "weapon")
    weapon(Item)
    kid_filter(RuleID, "keyword", KWList)
    has_keyword(Item, FilterKW)
    FilterKW in KWList
    => add_keyword(Item, KW)
```

One rule per item type (weapon, armor, ammo, etc.).

### Exclude Handling

Excludes are negated filters checked after the positive match:

```
_spid_kw_by_keyword(NPC, RuleID):
    spid_dist(RuleID, "keyword", Target)
    spid_filter(RuleID, "keyword", KWList)
    npc(NPC)
    has_keyword(NPC, KW)
    KW in KWList
    not spid_excluded(NPC, RuleID)
    => add_keyword(NPC, Target)

# Derived rule: NPC is excluded if any exclude filter matches
spid_excluded(NPC, RuleID):
    spid_exclude(RuleID, "keyword", ExKWList)
    has_keyword(NPC, ExKW)
    ExKW in ExKWList

spid_excluded(NPC, RuleID):
    spid_exclude(RuleID, "race", RaceList)
    race_of(NPC, Race)
    Race in RaceList
```

### Level Range Handling

```
_spid_level_ok(NPC, RuleID):
    spid_level(RuleID, Min, Max)
    base_level(NPC, Level)
    Level >= Min     # when Min > 0
    Level <= Max     # when Max > 0
```

## Implementation Changes

### INI Parsers (spid_parser.cpp, kid_parser.cpp)

Replace rule generation with fact emission. The parsers produce `std::vector<Tuple>` keyed by relation name instead of `std::vector<Rule>`. The caller adds these tuples to the FactDB.

New return type:
```cpp
struct IniFactSet {
    std::vector<std::pair<StringId, Tuple>> facts;
};
```

### Evaluator

No changes to the core evaluator. The generic rules are registered as normal Module rules (a synthetic module created at compile time, like the current INI import module).

The `in` operator needs to work with list Values from FactDB bindings. Currently `InClause` only handles literal value lists in source code. Extend it to also accept a bound variable that holds a list Value.

### FactDB / IndexedRelation

Add support for `Value::Kind::List` in storage and hashing. List values are stored as-is in tuples. Matching: a list value in a query pattern matches any stored list (exact match on the list), but a var in the query position matches any value including lists.

### Name Resolver

Register the new relations as builtins:
- `spid_dist`, `spid_filter`, `spid_exclude`, `spid_level`, `spid_chance`, `spid_count`
- `kid_dist`, `kid_filter`, `kid_exclude`

### Generic Rule Registration

A new function `register_ini_distribution_rules(Module& mod, StringPool& pool)` that programmatically constructs the ~20 generic rules and adds them to a synthetic module. Called from `cmd_compile` after INI facts are loaded.

## Performance Expectations

- 12,000 INI lines → 12,000 `spid_dist`/`kid_dist` facts + ~15,000 filter facts
- ~20 generic rules instead of 12,000
- Each generic rule does one bulk join (indexed on distribution type + filter kind)
- Query cache hits on every repeated pattern
- Expected: evaluation phase drops from minutes to seconds

## Migration

The old `import_ini_files` function that produced a Module with Rules is replaced by one that produces facts. The `cmd_import` pretty-printer can still work by reading the facts back and formatting them.

## Out of Scope

- Wildcard/glob matching on editor_id strings (needs a `matches_glob` predicate — separate feature)
- Trait filters (male/female/unique — needs NPC trait facts from ESP extraction)
- Chance/count (runtime behavior, not compile-time)
- SkyPatcher INI import (different format entirely)
