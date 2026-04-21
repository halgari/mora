# KID INI Grammar Reference

Derived from source analysis of powerof3/Keyword-Item-Distributor.
All behavior documented here matches KID as of commit 2026-Q1.

## 1. Overall INI Structure

### File Naming Convention

KID discovers files by scanning `Data/` (non-recursively) for files
whose names contain the substring `_KID` and whose extension is `.ini`:

```cpp
// distribution::get_configs in CLIBUtil/distribution.hpp
std::vector<std::string> configs = distribution::get_configs(R"(Data\)", "_KID"sv);
```

Files are sorted lexicographically after discovery — load order within
KID is alphabetical by full path, independent of plugin load order.

### Section Headers

KID reads only the **default (unnamed) section**. Any text under a
`[Section]` header is silently ignored. Modders must not use section
headers.

### Comment Conventions

From CSimpleIniA's `IsComment`:

```cpp
return (ch == ';' || ch == '#');
```

Both `;` and `#` introduce full-line comments. Comments must start at
the beginning of a line. No inline/end-of-line comments. `//` is NOT
a comment character.

### INI Key Structure

Each non-comment line is `key = rule_value`:

```
Keyword = formID~esp(OR)keywordEditorID|type|strings,formIDs(OR)editorIDs|traits|chance
```

The INI key is the keyword to distribute; the value is the rule body.
CSimpleIni supports multi-key — same key can appear multiple times,
each being an independent rule.

### Encoding

UTF-8 (with or without BOM; CSimpleIni strips BOMs).

## 2. Rule Line Syntax

```
<Keyword> = <Type>|<Filters>|<Traits>|<Chance>
```

Field indices (from KID's `TYPE` enum):

| Index | Field | Meaning |
|-------|-------|---------|
| (key) | Keyword | INI key — the keyword being distributed |
| 0     | Type   | Record type string (Weapon, Armor, ...) |
| 1     | Filters | Comma-separated filter list |
| 2     | Traits  | Comma-separated trait list |
| 3     | Chance  | Float 0-100, default 100 |

Missing trailing fields are absent (not empty strings).

### Canonical examples

```ini
; Add WeapMaterialIron to all weapons
WeapMaterialIron = Weapon

; One-handed swords only (trait filter)
WeapMaterialSteel = Weapon|||OneHandSword

; Armor, light only
ArmorMaterialLeather = Armor|||LIGHT

; Heavy armor with AR in range
ArmorMaterialIron = Armor|||HEAVY,AR(20 100)

; Weapon filtered by editor-ID match
WeapTypeGreatsword = Weapon|WeapIronSword

; All weapons in Skyrim.esm
WeapMaterialDaedric = Weapon|Skyrim.esm

; Weapon by FormID+plugin
WeapMaterialEbony = Weapon|0x1E718~Skyrim.esm

; Unenchanted bows, 75% chance
WeapTypeBow = Weapon|-E|Bow|75

; Armor NOT using body slot 32
ArmorHelmet = Armor|-32

; Potions that are NOT food
AlchPoison = Potion|||-F
```

## 3. Field Separators + Grammar

### Top-level

Value string splits on `|`. No escaping. Up to 4 tokens after the
key: `[Type, Filters, Traits, Chance]`.

### Filter sub-delimiter

Filters within field 1 are comma-separated. `NONE` (case-insensitive)
in any field means "absent".

### Filter prefix operators

Each comma-separated filter token's leading character determines its bucket:

| Prefix | Bucket  | Logic |
|--------|---------|-------|
| `+`    | ALL     | All entries in this bucket must match (AND) |
| `-`    | NOT     | Item must NOT match any of these |
| `*`    | ANY     | Substring/contains check |
| none   | MATCH   | At least one must match (OR) |

Evaluation in `PassedFilters`:
1. Skip if item already has the keyword
2. Skip if a mutually exclusive keyword is present
3. Fail chance gate (if < 100)
4. ALL: every entry must match
5. NOT: fail if any match
6. MATCH: at least one must match
7. ANY: substring match
8. Traits: type-specific

## 4. Reference Syntax

Reference resolution via `get_record_type`:

```cpp
if (a_str.contains("~")) return kFormIDMod;   // 0x1234~Plugin.esp
if (is_mod_name(a_str))  return kMod;          // contains ".es"
if (is_only_hex(a_str))  return kFormID;       // bare hex
return kEditorID;                              // anything else
```

- **Editor ID**: `WeapMaterialIron` — looked up via `LookupByEditorID`
- **FormID~Plugin**: `0x1E718~Skyrim.esm` — `LookupForm(formID, modName)`
- **Plugin name alone**: `Skyrim.esm` — matches all items from that plugin
- **Bare hex**: `0x1E718` — `LookupByID(formID)`; shifts with load order (discouraged)

## 5. Filter Types

### Form filters

MATCH/NOT/ALL entries that resolve to a TESForm go through `HasFormFilter`.
Dispatches on the form's type:

- Weapon/Ammo/Scroll/Book/Key/SoulGem/Flora/Activator/Furniture/Race/TalkingActivator: identity (`item == filterForm`)
- Keyword: item has this keyword (or its costliest magic effect does, etc.)
- Armor: identity; or for Race, matches `race->skin`
- Enchantment: weapon/armor's `formEnchanting`
- MagicEffect: any effect's `baseEffect`
- Spell: book's taught spell, race's actor effects, etc.
- FormList: recursively checks members
- Projectile/Location/EquipSlot/Perk/LeveledItem: type-specific lookup

Plugin filters use `file->IsFormInMod(item->GetFormID())`.

### String filters (MATCH/NOT/ALL with string-typed entries)

When get_record returns a string (editor-ID not found as a form, and
no `.nif` extension):

1. Case-insensitive exact match against editor-ID or display name
2. ActorValue name (weapons-by-skill, spells-by-school, books-by-skill)
3. Archetype name (magic effects)
4. `.nif` suffix: exact model path match

### ANY filters (`*` prefix)

Substring check:
- Contains `.nif` → substring against model path
- Otherwise → case-insensitive substring (`icontains`) against edid, name, or any keyword edid

### Trait filters (field 2)

Record-type-specific. See per-type tables in sections 6.1-6.10 of the
research report.

## 6. Record-Type Dispatch

The second field of the value (after the first `|`) is the type string:

- Armor, Weapon, Ammo, Magic Effect, Potion, Scroll, Location, Ingredient,
  Book, Misc Item, Key, Soul Gem, Spell, Activator, Flora, Furniture,
  Race, Talking Activator, Enchantment

Mora M3 implements only **Armor** and **Weapon**. Other types are parsed
into the AST but the distributor logs and skips them.

## 7. Trait Reference (Weapon + Armor)

### Weapon traits

| String         | Meaning                     |
|----------------|-----------------------------|
| OneHandSword   | one-handed sword animation  |
| OneHandDagger  | one-handed dagger           |
| OneHandAxe     | one-handed axe              |
| OneHandMace    | one-handed mace             |
| TwoHandSword   | two-handed sword/greatsword |
| TwoHandAxe     | two-handed axe              |
| Bow            | bow                         |
| Crossbow       | crossbow                    |
| Staff          | staff                       |
| HandToHandMelee | unarmed weapon type        |
| E              | Has enchantment             |
| -E             | No enchantment              |
| T              | Has template                |
| -T             | No template                 |
| D(min max)     | Damage in range (float)     |
| W(min max)     | Weight in range (float)     |

### Armor traits

| String         | Meaning                          |
|----------------|----------------------------------|
| HEAVY          | heavy armor type                 |
| LIGHT          | light armor type                 |
| CLOTHING       | clothing                         |
| E              | Has enchantment                  |
| -E             | No enchantment                   |
| T              | Has template                     |
| -T             | No template                      |
| AR(min max)    | Armor rating in range            |
| W(min max)     | Weight in range                  |
| 30-61 (number) | Covers BipedObjectSlot with that number |

## 8. Edge Cases

- **Default chance** (omitted or `NONE`): `100.0` — applies to every matching item.
- **Empty filter bucket**: short-circuits to "pass".
- **Whitespace** in fields: NOT stripped by KID's parser (spaces become part of tokens). Mora follows KID. CSimpleIni does strip around the `=`.
- **Case sensitivity**:
  - Type field: case-sensitive ("Weapon" not "weapon")
  - Trait strings: case-sensitive (`HEAVY` not `heavy`)
  - String filter editor-IDs: case-insensitive (iequals / icontains)
  - `NONE` sentinel: case-insensitive
- **Parse errors**: KID skips the offending line and logs. Mora does the same via `tracing::warn`.
- **ExclusiveGroup** (`ExclusiveGroup = GroupName|Keyword1,Keyword2,…`): special reserved key. M3 ignores — planned for Plan 7+.

## ABNF Grammar

```abnf
kid-file     = *line
line         = comment-line / rule-line / excl-line / blank-line
comment-line = (";" / "#") *VCHAR CRLF
rule-line    = keyword "=" type "|" filters "|" traits "|" chance CRLF
             / keyword "=" type "|" filters "|" traits CRLF
             / keyword "=" type "|" filters CRLF
             / keyword "=" type CRLF
keyword      = ref
type         = "Weapon" / "Armor" / "Ammo" / ...
filters      = filter *("," filter) / "NONE" / ""
filter       = and-filter / not-filter / any-filter / match-filter
and-filter   = ref "+" ref *("+" ref)
not-filter   = "-" ref
any-filter   = "*" 1*VCHAR
match-filter = ref
traits       = trait *("," trait) / "NONE" / ""
chance       = 1*3DIGIT ["." 1*2DIGIT]
ref          = editorid / formid-mod / modname / hex-formid
```
