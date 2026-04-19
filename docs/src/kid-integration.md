# KID Integration (`*_KID.ini`)

Mora can ingest [KID (Keyword Item Distributor)](https://www.nexusmods.com/skyrimspecialedition/mods/55728) `.ini` files at compile time and bake the keyword attachments directly into the patch output. With KID baked, you don't need powerof3's SKSE DLL at runtime — the keywords are already on the records by the time the game loads.

## How it works

1. `mora compile --data-dir <Data>` walks `<Data>` for `*_KID.ini` files.
2. The KID compiler (`mora_skyrim_compile/kid_compiler.h`) parses each line, resolves every reference (EditorID lookups against the load order, FormID-ref globalisation for `0xFFF~Mod.esp`, glob expansion of wildcards), and **emits one Mora rule per OR-group** of the line. Each synthesized rule has the shape:
   ```mora
   skyrim/add(X, :Keyword, @TargetKW):
       form/<item_type>(X)
       [form/enchanted_with(X, _) | not form/enchanted_with(X, _)]   # E / -E traits
       form/keyword(X, @MemberA)                                     # AND-members
       form/keyword(X, @MemberB)
   ```
   AND-of-ORs is encoded as one rule per OR-group: `A+B,C` produces two rules whose bodies share the type predicate but differ in their conjuncts.
3. The synthesized module is appended to the program before evaluation. The same evaluator that handles user `.mora` rules processes the KID rules — there's no separate KID interpreter or stdlib wiring.
4. The sinks you configure (`--sink parquet.snapshot=...`) receive the resulting `skyrim/add` effects.

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

## Supported grammar

```
TargetKeyword | ItemType | FilterStrings | Traits | Chance
```

| Field | Notes |
|---|---|
| `TargetKeyword` | EditorID (e.g. `MyKeyword`) or `0xFFFFFF~Mod.esp`. Both are resolved against the load order. ESL/light refs use the `0xFE00xxxx` encoding automatically. Unknown plugins produce `kid-missing-plugin`. |
| `ItemType` | One of the 19 KID item types: `Weapon`, `Armor`, `Ammo`, `MagicEffect`, `Potion`, `Scroll`, `Location`, `Ingredient`, `Book`, `MiscItem`, `Key`, `SoulGem`, `Spell`, `Activator`, `Flora`, `Furniture`, `Race`, `TalkingActivator`, `Enchantment`. Case-insensitive; spaces allowed (`Magic Effect`). |
| `FilterStrings` | Comma-separated OR-groups, with `+` joining an AND-group inside a group. `A+B,C` matches items that have *both* A and B, *or* C. Each OR-group becomes its own synthesized rule. Glob patterns (`*Iron`, `Iron?`) expand against the EditorID map at resolve time; wildcards inside an AND-group are cross-producted with the other members (each match becomes a separate OR-alternative). |
| `Traits` | `E` / `-E` (enchanted / non-enchanted) are wired for weapon, armor, ammo. Other traits (`HEAVY`, `LIGHT`, `AR(min,max)`, body slots, spell/magic-effect flags) are parsed but not yet consumed. |
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
| `kid-wildcard-fanout` | A wildcard expanded to more than 1000 matches, or an AND-group's cross-product exceeded the 1024 cap. Usually a typo; consider narrowing. |
| `kid-chance-ignored` | `Chance < 100`. Line kept, applied unconditionally. |
| `kid-no-editor-ids` | No ESPs were loaded, so nothing resolves. Usually means `--data-dir` is wrong or the ESPs are missing. |

## Limitations

- **FULL name (display name) wildcards** — matching against item display names rather than EditorIDs isn't implemented.
- **Trait filters**: `E` / `-E` are honored (weapon, armor, ammo). `HEAVY` / `LIGHT` / `CLOTHING`, `AR(min,max)`, `W(min,max)`, body slots, spell/magic-effect flags are parsed but no synthesized rule consumes them yet. Tracked in `docs/specs/2026-04-18-kid-v2-deferred-features.md` waves 2-3.
- **Runtime chance rolls** are not simulated: `Chance < 100` applies unconditionally and produces a diagnostic so you can see where the fidelity gap is.
- **SPID** (Spell Perk Item Distributor) is not yet integrated.
