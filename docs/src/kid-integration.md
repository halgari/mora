# KID Integration (`*_KID.ini`)

Mora can ingest [KID (Keyword Item Distributor)](https://www.nexusmods.com/skyrimspecialedition/mods/55728) `.ini` files at compile time and bake the keyword attachments directly into the patch output. With KID baked, you don't need powerof3's SKSE DLL at runtime — the keywords are already on the records by the time the game loads.

## How it works

1. `mora compile --data-dir <Data>` walks `<Data>` for `*_KID.ini` files.
2. Each line is parsed into facts under the `ini/` namespace:
   - `ini/kid_dist(RuleID, TargetKW, ItemType)` — one row per line.
   - `ini/kid_filter(RuleID, "keyword", Value)` — one row per positive-filter value. OR semantics across rows for a given `RuleID`.
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
| `FilterStrings` | Comma-separated OR-groups. `+` joins an AND-group inside a group. In v1 AND-groups are flattened to OR (narrower intent than KID's original) — see Limitations. |
| `Traits` | Parsed but not consumed in v1. `E` / `-E` hit `ini/kid_trait` but no wiring rule reads them yet. |
| `Chance` | `100` or blank = always apply. Anything below 100 emits a `kid-chance-ignored` warning and applies the keyword unconditionally at compile time. |

Comments start with `;`. Bracketed section headers (`[Keywords]`) are accepted and ignored.

## Diagnostics

| Code | When |
|---|---|
| `kid-parse` | Malformed line (missing fields, bad hex, unknown item type). Line dropped. |
| `kid-unresolved` | An EditorID didn't match the ESP load order. Targets drop the line; filter values narrow the filter. |
| `kid-formid-unsupported` | `0xFFF~Mod.esp` reference but the caller didn't wire `plugin_runtime_index_out`. Library usage only; `mora compile` always sets it. |
| `kid-missing-plugin` | `0xFFF~Unknown.esp` references a plugin not in the resolved load order. Line dropped. |
| `kid-chance-ignored` | `Chance < 100`. Line kept, applied unconditionally. |
| `kid-no-editor-ids` | No ESPs were loaded, so nothing resolves. Usually means `--data-dir` is wrong or the ESPs are missing. |
| `stdlib-missing` | Mora couldn't find its stdlib and won't activate the wiring rules. Pass `--stdlib-dir` or set `$MORA_STDLIB`. |

## Limitations

- **Wildcard item matching** (`*Iron`) isn't supported.
- **AND-of-ORs** (`KW1+KW2`) is flattened to OR in v1: each value becomes its own `ini/kid_filter` row. The resulting match set is a superset of a strict AND but a subset of the original KID intent.
- **Trait filters** (`E`, `-E`, `HEAVY`, `LIGHT`, `AR(min,max)`, body slots) are parsed but not honored. Wiring needs `form/is_enchanted`, `form/armor_rating`, etc. — tracked as follow-up work.
- **Runtime chance rolls** are not simulated: `Chance < 100` applies unconditionally and produces a diagnostic so you can see where the fidelity gap is.
- **SPID** (Spell Perk Item Distributor) is not yet integrated. The schema leaves room for it (`spid_*` facts are stubbed in the legacy sema table) but the datasource/stdlib don't exist yet.
