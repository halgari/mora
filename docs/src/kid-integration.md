# KID Integration (`*_KID.ini`)

Mora can ingest [KID (Keyword Item Distributor)](https://www.nexusmods.com/skyrimspecialedition/mods/55728) `.ini` files at compile time and bake the keyword attachments directly into the patch output. With KID baked, you don't need powerof3's SKSE DLL at runtime — the keywords are already on the records by the time the game loads.

## How it works

1. `mora compile --data-dir <Data>` walks `<Data>` for `*_KID.ini` files.
2. Each line is parsed into facts under the `ini/` namespace:
   - `ini/kid_dist(RuleID, TargetKW, ItemType)` — one row per line.
   - `ini/kid_filter(RuleID, GroupID, "keyword", Value)` — AND-of-ORs: values sharing `(RuleID, GroupID)` are AND'd (KID's `+` operator); distinct `GroupID`s within the same `RuleID` are OR'd (KID's `,`).
   - `ini/kid_exclude(RuleID, "keyword", Value)` — negative filters.
   - `ini/kid_trait(RuleID, Trait)` — parsed but not yet consumed (see [Limitations](#limitations)).
3. Mora's bundled stdlib (`data/stdlib/kid.mora`) contains wiring rules — one per KID item type — that join these facts against `form/<item_type>(X)` and `form/keyword(X, KW)` to emit `skyrim/add(X, :Keyword, TargetKW)` effects.
4. The sinks you configure (`--sink parquet.snapshot=...`) receive the resulting patches.

## Usage

```bash
mora compile ./my_mods \
    --data-dir "$HOME/.steam/.../Skyrim Special Edition/Data" \
    --sink parquet.snapshot=./out
```

KID loading is automatic when `--data-dir` is set. Flags:

| Flag | Default | Meaning |
|---|---|---|
| `--no-kid` | off | Skip `*_KID.ini` entirely. Use this if you want KID's runtime DLL to keep handling distributions. |
| `--kid-dir PATH` | — | Override where `mora` looks for `*_KID.ini`. Defaults to `--data-dir`. Useful for test fixtures. |
| `--stdlib-dir PATH` | auto | Override the Mora stdlib directory (where `kid.mora` lives). Resolution order: this flag → `$MORA_STDLIB` → `<cwd>/data/stdlib` → `../data/stdlib`. |

## Supported grammar (v1)

```
TargetKeyword | ItemType | FilterStrings | Traits | Chance
```

| Field | Notes |
|---|---|
| `TargetKeyword` | EditorID (e.g. `MyKeyword`) or `0xFFFFFF~Mod.esp`. Both are resolved against the load order. ESL/light refs use the `0xFE00xxxx` encoding automatically. Unknown plugins produce `kid-missing-plugin`. |
| `ItemType` | One of the 19 KID item types: `Weapon`, `Armor`, `Ammo`, `MagicEffect`, `Potion`, `Scroll`, `Location`, `Ingredient`, `Book`, `MiscItem`, `Key`, `SoulGem`, `Spell`, `Activator`, `Flora`, `Furniture`, `Race`, `TalkingActivator`, `Enchantment`. Case-insensitive; spaces allowed (`Magic Effect`). |
| `FilterStrings` | Comma-separated OR-groups, with `+` joining an AND-group inside a group. `A+B,C` matches items that have *both* A and B, *or* C. Each group becomes a distinct `(RuleID, GroupID)` in `ini/kid_filter`; stdlib wiring rules join via negation-as-failure. Glob patterns (`*Iron`, `Iron?`) expand against the EditorID map at resolve time; wildcards inside an AND-group are dropped with a warning. |
| `Traits` | `E` / `-E` (enchanted / non-enchanted) are wired for weapon, armor, ammo. Other traits (`HEAVY`, `LIGHT`, `AR(min,max)`, body slots, spell/magic-effect flags) are parsed into `ini/kid_trait` but not yet consumed. |
| `Chance` | `100` or blank = always apply. Anything below 100 emits a `kid-chance-ignored` warning and applies the keyword unconditionally at compile time. |

Comments start with `;`. Bracketed section headers (`[Keywords]`) are accepted and ignored.

## Diagnostics

| Code | When |
|---|---|
| `kid-parse` | Malformed line (missing fields, bad hex, unknown item type). Line dropped. |
| `kid-unresolved` | An EditorID didn't match the ESP load order. Targets drop the line; filter values narrow the filter. |
| `kid-formid-unsupported` | `0xFFF~Mod.esp` reference but the caller didn't wire `plugin_runtime_index_out`. Library usage only; `mora compile` always sets it. |
| `kid-missing-plugin` | `0xFFF~Unknown.esp` references a plugin not in the resolved load order. Line dropped. |
| `kid-wildcard-empty` | A wildcard filter (`*Iron`) matched zero EditorIDs. Value dropped; other filter values on the line survive. |
| `kid-wildcard-all` | A filter value was the degenerate `*` pattern. Rejected; use an empty / no-filter line if the intent is "all items of the type". |
| `kid-wildcard-in-and` | A wildcard appeared inside a `+` AND-group (e.g. `*Iron+Heavy`). v2 can't express this cleanly; the wildcard is dropped and the AND-group proceeds without it. |
| `kid-wildcard-fanout` | A wildcard expanded to more than 1000 matches — usually a typo. Expansion proceeds but build times suffer; consider narrowing. |
| `kid-chance-ignored` | `Chance < 100`. Line kept, applied unconditionally. |
| `kid-no-editor-ids` | No ESPs were loaded, so nothing resolves. Usually means `--data-dir` is wrong or the ESPs are missing. |
| `stdlib-missing` | Mora couldn't find its stdlib and won't activate the wiring rules. Pass `--stdlib-dir` or set `$MORA_STDLIB`. |

## Limitations

- **Wildcards inside AND-groups** — `*Iron+Heavy` can't express "EditorID matches `*Iron` AND has Heavy keyword" because expansion happens before the AND-join. The wildcard is dropped with `kid-wildcard-in-and`. Wildcards in plain OR positions (`*Iron,*Gold`) expand cleanly.
- **FULL name (display name) wildcards** — matching against item display names rather than EditorIDs isn't implemented.
- **Trait filters**: `E` / `-E` are honored (weapon, armor, ammo). `HEAVY` / `LIGHT` / `CLOTHING`, `AR(min,max)`, `W(min,max)`, body slots, spell/magic-effect flags are parsed into `ini/kid_trait` but no wiring rule reads them. Tracked in `docs/specs/2026-04-18-kid-v2-deferred-features.md` waves 2-3.
- **Runtime chance rolls** are not simulated: `Chance < 100` applies unconditionally and produces a diagnostic so you can see where the fidelity gap is.
- **SPID** (Spell Perk Item Distributor) is not yet integrated. The schema leaves room for it (`spid_*` facts are stubbed in the legacy sema table) but the datasource/stdlib don't exist yet.
