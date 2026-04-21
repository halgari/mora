# Rust + KID Pivot — Plan 6: `mora-kid` MVP (M3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver `mora-kid` as a library: KID INI discovery + parser + a `KidDistributor` implementing `mora_core::Distributor<EspWorld>`. Given a set of `*_KID.ini` files and an `EspWorld`, `mora-kid` produces a `Vec<Patch>` ready for `PatchSink`. Weapon + Armor support only at this plan; full integration through a CLI is Plan 7.

**Architecture:** `mora-kid` is a pure-Rust crate. Depends on `mora-core` (types) and `mora-esp` (EspWorld, record iteration). INI parsing is hand-written line-based (KID's grammar is simple pipe-delimited key-value per line). Reference resolution uses a small `Reference` enum dispatching between editor-ID/FormID~Plugin/plugin-only/bare-hex forms. Filters are bucketed into MATCH/NOT following KID. Traits are record-type-specific structs. The distributor scans candidates via `world.weapons()` / `world.armors()`, evaluates each rule's filter pipeline, and emits `Patch::AddKeyword` via the sink.

**Tech Stack:** Rust 1.90. No new workspace deps; `miette` is already workspace-pinned for diagnostics. Plan 5's `mora-esp` is extended with a `Keyword` (KYWD) record type + `EspWorld::keywords()` iterator (tasks 3-4), since resolving editor-ID references requires knowing which FormIDs back which keyword editor-IDs.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope discipline:**
- **Record types at M3:** Weapon + Armor only. Rules targeting other types (Potion, NPC, Scroll, etc.) are parsed but distribution is skipped with a `tracing::warn` (real distribution lands in Plan 8+ per-type).
- **Filter buckets at M3:** `MATCH` (OR, plain) and `NOT` (`-` prefix). `ALL` (`+`) and `ANY` (`*`) filters are parsed into the AST but the distributor logs-and-skips — Plan 7 or 8 activates them.
- **ExclusiveGroups:** not parsed at M3. The `ExclusiveGroup = …` lines are silently ignored (defer to Plan 7).
- **No CLI wire-up.** `mora_kid::pipeline::compile(world, inis) -> Vec<Patch>` is the public entry. mora-cli gets its first real end-to-end compile in Plan 7.
- **No bit-identity verification yet.** M4 (Plan 8 or wherever it lands) captures real KID output and diffs — we design for it but don't test it here.

---

## File Structure

**Modified:**
- `crates/mora-esp/src/signature.rs` — add `KYWD` constant
- `crates/mora-esp/src/records/mod.rs` — add `keyword` submodule
- `crates/mora-esp/src/world.rs` — add `keywords()` iterator + `resolve_keyword_by_editor_id()` helper
- `crates/mora-kid/src/lib.rs` — replace stub with module tree + re-exports
- `crates/mora-kid/Cargo.toml` — already has `mora-core`, `mora-esp`, `thiserror`, `miette`, `tracing` from Plan 0

**Created:**
- `crates/mora-esp/src/records/keyword.rs` — KYWD (keyword) record accessor
- `crates/mora-esp/tests/keyword_record.rs` — KYWD integration tests
- `crates/mora-kid/src/reference.rs` — `Reference` enum + resolver
- `crates/mora-kid/src/rule.rs` — `KidRule` AST
- `crates/mora-kid/src/filter.rs` — `FilterBuckets` + evaluation
- `crates/mora-kid/src/traits_weapon.rs` — Weapon trait parser
- `crates/mora-kid/src/traits_armor.rs` — Armor trait parser
- `crates/mora-kid/src/ini.rs` — INI file discovery + line parsing
- `crates/mora-kid/src/distributor.rs` — `KidDistributor` implementing `Distributor<EspWorld>`
- `crates/mora-kid/src/pipeline.rs` — top-level `compile()` entry point
- `crates/mora-kid/tests/parse.rs` — INI parsing integration tests
- `crates/mora-kid/tests/distribute.rs` — distributor end-to-end tests
- `docs/src/kid-ini-grammar.md` — KID grammar reference (from the Plan 6 research agent report)

---

## Phase A — KID grammar reference doc (Task 1)

### Task 1: Write `docs/src/kid-ini-grammar.md`

**Files:**
- Create: `docs/src/kid-ini-grammar.md`

This is the source-of-truth for every parser task in this plan.

- [ ] **Step 1: Write the doc**

Lift the Plan 6 research report (compiled from KID's source) verbatim into the reference file. The report structure:

1. Overall INI structure (file discovery, sections, comments, encoding)
2. Rule line syntax (`Keyword = Type|Filters|Traits|Chance`)
3. Field separators + grammar (filter prefix dispatch: `+`/`-`/`*`/none)
4. Reference syntax (editor-ID, FormID~Plugin, plugin-only, bare hex)
5. Filter types (form, string, keyword, trait)
6. Record-type dispatch
7. Edge cases (default chance, empty filters, whitespace, case-sensitivity, error handling, ExclusiveGroups)
8. Summary grammar in ABNF

```bash
cd /home/tbaldrid/oss/mora
# Write the exhaustive KID grammar reference. This document is the
# output of Plan 6's research subagent (analysis of powerof3/KID source).
# The full content is ~350 lines; paste verbatim from the research-agent
# report captured during plan authoring.
cat > docs/src/kid-ini-grammar.md <<'EOF'
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

EOF
```

- [ ] **Step 2: Commit**

```bash
git add docs/src/kid-ini-grammar.md
git commit -m "docs: KID INI grammar reference

Derived from source analysis of powerof3/Keyword-Item-Distributor.
Covers file discovery, rule-line syntax, field separators, filter
bucket dispatch (MATCH/NOT/ALL/ANY via prefix), reference resolution
(editor-ID / FormID~Plugin / plugin-only / bare hex), form/string/
trait filters, record-type dispatch, Weapon + Armor trait tables,
edge cases (whitespace, case-sensitivity, error handling), and ABNF
summary. Cited by every parser task in Plan 6."
```

---

## Phase B — `mora-esp` extension: KYWD record (Tasks 2-3)

mora-kid needs to resolve editor-IDs to FormIDs for keyword references. KID searches for editor-IDs first in keyword records (KYWD), then in any form. M3 narrows to keyword editor-IDs only (sufficient for Weapon/Armor rules).

### Task 2: Add `records/keyword.rs` + `KYWD` signature constant

**Files:**
- Modify: `crates/mora-esp/src/signature.rs`
- Modify: `crates/mora-esp/src/records/mod.rs`
- Create: `crates/mora-esp/src/records/keyword.rs`

- [ ] **Step 1: Add KYWD signature**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/signature.rs")
text = p.read_text()
if "KYWD" not in text:
    marker = "pub const XXXX: Signature = Signature::new(b\"XXXX\");\n"
    new_line = "pub const KYWD: Signature = Signature::new(b\"KYWD\");\n"
    text = text.replace(marker, marker + new_line, 1)
    p.write_text(text)
PY
grep KYWD crates/mora-esp/src/signature.rs
```

- [ ] **Step 2: Add `keyword` submodule**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/records/mod.rs")
text = p.read_text()
if "pub mod keyword" not in text:
    text = text.replace("pub mod armor;", "pub mod armor;\npub mod keyword;")
    p.write_text(text)
PY
grep '^pub mod' crates/mora-esp/src/records/mod.rs
```

- [ ] **Step 3: Write records/keyword.rs**

```bash
cat > crates/mora-esp/src/records/keyword.rs <<'EOF'
//! `KYWD` — keyword record accessor.
//!
//! A keyword record has only one subrecord of interest to Mora:
//! `EDID` (editor ID). Keywords typically do not carry KWDA themselves.

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::EDID;
use crate::subrecord::SubrecordIter;
use crate::subrecords::edid;

#[derive(Debug, thiserror::Error)]
pub enum KeywordError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

/// Parsed KYWD record.
#[derive(Debug, Default)]
pub struct KeywordRecord {
    pub editor_id: Option<String>,
}

/// Parse a KYWD record.
pub fn parse(record: &Record<'_>) -> Result<KeywordRecord, KeywordError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut k = KeywordRecord::default();
    let iter = SubrecordIter::new(body);
    for sub in iter {
        let sub = sub?;
        if sub.signature == EDID {
            k.editor_id = Some(edid::parse(sub.data)?);
        }
    }
    Ok(k)
}
EOF
```

- [ ] **Step 4: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-esp --all-targets
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/signature.rs crates/mora-esp/src/records/
git commit -m "mora-esp: records::keyword — KYWD record accessor

KYWD signature added; parse(record) -> KeywordRecord { editor_id }.
Enables mora-kid's editor-ID reference resolution in Plan 6."
```

---

### Task 3: `EspWorld::keywords()` + `resolve_keyword_by_editor_id`

**Files:**
- Modify: `crates/mora-esp/src/world.rs`

- [ ] **Step 1: Append keyword iterator + resolver to world.rs**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/world.rs")
text = p.read_text()

# Add KYWD import to the signature use line.
old_sig_line = "use crate::signature::{ARMO, Signature, WEAP};"
new_sig_line = "use crate::signature::{ARMO, KYWD, Signature, WEAP};"
assert old_sig_line in text, "expected signature import not found"
text = text.replace(old_sig_line, new_sig_line)

# Add keyword to records import. The existing line reads
# "use crate::records::{armor, weapon};"
old_rec_line = "use crate::records::{armor, weapon};"
new_rec_line = "use crate::records::{armor, keyword, weapon};"
assert old_rec_line in text, "expected records import not found"
text = text.replace(old_rec_line, new_rec_line)

# Append new methods to the impl EspWorld block. Find the closing of
# the armors() method, insert after.
marker = """    pub fn armors(
        &self,
    ) -> impl Iterator<Item = Result<(FormId, armor::ArmorRecord), armor::ArmorError>> + '_ {
        self.records(ARMO).map(move |wr| {
            let parsed = armor::parse(&wr.record, wr.plugin_index, self)?;
            Ok((wr.resolved_form_id, parsed))
        })
    }
}"""

replacement = """    pub fn armors(
        &self,
    ) -> impl Iterator<Item = Result<(FormId, armor::ArmorRecord), armor::ArmorError>> + '_ {
        self.records(ARMO).map(move |wr| {
            let parsed = armor::parse(&wr.record, wr.plugin_index, self)?;
            Ok((wr.resolved_form_id, parsed))
        })
    }

    /// Iterate all KYWD records, parsed. Each item is
    /// `(record_form_id, parsed_keyword)`.
    pub fn keywords(
        &self,
    ) -> impl Iterator<Item = Result<(FormId, keyword::KeywordRecord), keyword::KeywordError>> + '_
    {
        self.records(KYWD).map(move |wr| {
            let parsed = keyword::parse(&wr.record)?;
            Ok((wr.resolved_form_id, parsed))
        })
    }

    /// Resolve a keyword's editor-ID to its runtime FormId. Case
    /// sensitivity matches KID: case-insensitive match.
    /// Returns None if no keyword with that editor-ID is found.
    pub fn resolve_keyword_by_editor_id(&self, editor_id: &str) -> Option<FormId> {
        for entry in self.keywords() {
            let Ok((fid, kw)) = entry else { continue };
            if let Some(edid) = kw.editor_id.as_deref()
                && edid.eq_ignore_ascii_case(editor_id)
            {
                return Some(fid);
            }
        }
        None
    }
}"""
assert marker in text, "expected end-of-armors marker not found"
text = text.replace(marker, replacement)
p.write_text(text)
PY
```

- [ ] **Step 2: Extend integration test — confirm KYWD records round-trip**

```bash
cat >> crates/mora-esp/tests/esp_format.rs <<'EOF'

#[test]
fn world_resolves_keyword_editor_id() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"KYWD").add(
                RecordBuilder::new(b"KYWD", 0x0001_E718).add(SubrecordBuilder::new(
                    b"EDID",
                    edid_payload("WeapMaterialIron"),
                )),
            ),
        )
        .bytes();

    let path = write_tmp("KeywordPlugin.esm", &plugin_bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-kw.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    assert_eq!(
        world.resolve_keyword_by_editor_id("WeapMaterialIron"),
        Some(FormId(0x0001_E718))
    );
    assert_eq!(
        world.resolve_keyword_by_editor_id("WEAPMATERIALIRON"), // case-insensitive
        Some(FormId(0x0001_E718))
    );
    assert_eq!(world.resolve_keyword_by_editor_id("UnknownKw"), None);
}
EOF
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-esp --test esp_format world_resolves_keyword_editor_id
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/world.rs crates/mora-esp/tests/esp_format.rs
git commit -m "mora-esp: EspWorld::keywords() + resolve_keyword_by_editor_id

keywords() iterator yields (FormId, KeywordRecord) pairs.
resolve_keyword_by_editor_id(edid) scans for a matching editor-ID
(case-insensitive, matches KID iequals). Required by mora-kid for
editor-ID reference resolution."
```

---

## Phase C — mora-kid crate setup (Task 4)

### Task 4: mora-kid crate scaffold

**Files:**
- Modify: `crates/mora-kid/src/lib.rs`
- Create: stubs for every mora-kid module

- [ ] **Step 1: Rewrite lib.rs**

```bash
cat > crates/mora-kid/src/lib.rs <<'EOF'
//! KID INI parser + distributor frontend.
//!
//! M3 supports Weapon + Armor record types; rules targeting other
//! types are parsed but skipped at distribute time with a warning.
//! See `docs/src/kid-ini-grammar.md` for the full grammar.

pub mod distributor;
pub mod filter;
pub mod ini;
pub mod pipeline;
pub mod reference;
pub mod rule;
pub mod traits_armor;
pub mod traits_weapon;

pub use distributor::{KidDistributor, KidError};
pub use pipeline::{compile, CompileError};
pub use reference::Reference;
pub use rule::{KidRule, RecordType};
EOF
```

- [ ] **Step 2: Create stub modules**

```bash
for m in distributor filter ini pipeline reference rule traits_armor traits_weapon; do
    cat > "crates/mora-kid/src/$m.rs" <<EOF
//! Stub. Populated in its own task of Plan 6.
EOF
done
```

Three modules need placeholder types for `lib.rs` re-exports:

```bash
cat > crates/mora-kid/src/distributor.rs <<'EOF'
//! Stub. Populated in Task 14.

/// Placeholder — real impl in Task 14.
pub struct KidDistributor;

/// Placeholder — real impl in Task 14.
#[derive(Debug, thiserror::Error)]
pub enum KidError {
    #[error("stub")]
    Stub,
}
EOF

cat > crates/mora-kid/src/pipeline.rs <<'EOF'
//! Stub. Populated in Task 15.

/// Placeholder — real impl in Task 15.
#[derive(Debug, thiserror::Error)]
pub enum CompileError {
    #[error("stub")]
    Stub,
}

/// Placeholder — real impl in Task 15.
pub fn compile() {}
EOF

cat > crates/mora-kid/src/reference.rs <<'EOF'
//! Stub. Populated in Task 5.

/// Placeholder — real impl in Task 5.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Reference {
    /// Editor-ID (e.g. "WeapMaterialIron")
    EditorId(String),
}
EOF

cat > crates/mora-kid/src/rule.rs <<'EOF'
//! Stub. Populated in Task 8.

/// Placeholder — real impl in Task 8.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RecordType {
    Weapon,
    Armor,
    Other(String),
}

/// Placeholder — real impl in Task 8.
#[derive(Debug, Clone)]
pub struct KidRule {
    pub keyword: crate::reference::Reference,
    pub record_type: RecordType,
}
EOF
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-kid/
git commit -m "mora-kid: crate scaffold with module tree + stubs

Modules: distributor, filter, ini, pipeline, reference, rule,
traits_armor, traits_weapon. All stubs typed so cargo check stays
green through Plan 6."
```

---

## Phase D — Reference types + resolver (Tasks 5-6)

### Task 5: Implement `reference.rs` — Reference enum + parser

**Files:**
- Modify: `crates/mora-kid/src/reference.rs`

- [ ] **Step 1: Write reference.rs**

```bash
cat > crates/mora-kid/src/reference.rs <<'EOF'
//! Reference — a KID-style form / plugin / editor-ID reference.
//!
//! KID's `get_record_type` dispatches on the content of the string:
//!   - contains `~` → `FormIdWithPlugin`
//!   - contains `.es` → `PluginName`
//!   - only hex chars (optional `0x` prefix) → `FormIdOnly`
//!   - else → `EditorId`

use mora_core::FormId;
use mora_esp::EspWorld;

/// A KID reference.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Reference {
    /// Editor-ID string, e.g. `"WeapMaterialIron"`.
    EditorId(String),
    /// FormID (3-byte local) paired with the defining plugin name,
    /// e.g. `"0x01E718~Skyrim.esm"` or `"0x1E718~Skyrim.esm"`.
    FormIdWithPlugin {
        local_id: u32,
        plugin: String,
    },
    /// A plugin filename (no form), used as a file filter.
    PluginName(String),
    /// A bare hex FormID with no plugin qualifier. Load-order-sensitive.
    FormIdOnly(u32),
}

impl Reference {
    /// Parse a single KID reference string.
    pub fn parse(s: &str) -> Self {
        if let Some(tilde) = s.find('~') {
            let (hex, plugin) = s.split_at(tilde);
            let plugin = &plugin[1..]; // strip '~'
            if let Some(local) = parse_hex(hex) {
                return Reference::FormIdWithPlugin {
                    local_id: local,
                    plugin: plugin.to_string(),
                };
            }
        }
        if is_mod_name(s) {
            return Reference::PluginName(s.to_string());
        }
        if let Some(raw) = parse_hex(s) {
            return Reference::FormIdOnly(raw);
        }
        Reference::EditorId(s.to_string())
    }

    /// Resolve this reference to a FormId using the loaded world.
    /// Returns None if the reference cannot be resolved (missing
    /// plugin, unknown editor-ID, etc.).
    ///
    /// Note: `PluginName` references are NOT forms — they're plugin
    /// filters. `resolve_form` returns None for them. Callers
    /// distinguish with `matches!(r, Reference::PluginName(_))` when
    /// they need plugin filter semantics.
    pub fn resolve_form(&self, world: &EspWorld) -> Option<FormId> {
        match self {
            Reference::EditorId(edid) => world.resolve_keyword_by_editor_id(edid),
            Reference::FormIdWithPlugin { local_id, plugin } => {
                // Find the plugin's slot in the load order.
                let slot = world.load_order.lookup(plugin)?;
                Some(FormId(slot.compose_form_id(*local_id & 0x00FF_FFFF)))
            }
            Reference::PluginName(_) => None,
            Reference::FormIdOnly(raw) => Some(FormId(*raw)),
        }
    }
}

fn parse_hex(s: &str) -> Option<u32> {
    let s = s.trim();
    let stripped = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")).unwrap_or(s);
    if stripped.is_empty() || !stripped.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    u32::from_str_radix(stripped, 16).ok()
}

fn is_mod_name(s: &str) -> bool {
    s.to_ascii_lowercase().contains(".es")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_editor_id() {
        assert_eq!(
            Reference::parse("WeapMaterialIron"),
            Reference::EditorId("WeapMaterialIron".into())
        );
    }

    #[test]
    fn parses_form_id_with_plugin() {
        assert_eq!(
            Reference::parse("0x1E718~Skyrim.esm"),
            Reference::FormIdWithPlugin {
                local_id: 0x1E718,
                plugin: "Skyrim.esm".into()
            }
        );
    }

    #[test]
    fn parses_plugin_name() {
        assert_eq!(
            Reference::parse("Skyrim.esm"),
            Reference::PluginName("Skyrim.esm".into())
        );
        assert_eq!(
            Reference::parse("MyMod.esp"),
            Reference::PluginName("MyMod.esp".into())
        );
        assert_eq!(
            Reference::parse("LightMod.esl"),
            Reference::PluginName("LightMod.esl".into())
        );
    }

    #[test]
    fn parses_bare_hex() {
        assert_eq!(Reference::parse("0x1E718"), Reference::FormIdOnly(0x1E718));
        // No prefix, just hex:
        assert_eq!(Reference::parse("1E718"), Reference::FormIdOnly(0x1E718));
    }

    #[test]
    fn editor_id_with_digits_is_still_editor_id() {
        // Editor IDs often contain digits; what matters is that they
        // don't consist ONLY of hex chars (which would parse as FormID).
        // "WeapMaterialIron01" is not all hex because W/M/L/R/N/S
        // aren't in [0-9a-fA-F].
        assert_eq!(
            Reference::parse("WeapMaterialIron01"),
            Reference::EditorId("WeapMaterialIron01".into())
        );
    }

    #[test]
    fn all_hex_editor_id_parses_as_form_id() {
        // A quirk matching KID: "DEADBEEF" is all-hex → parsed as FormID
        // even though the user might have meant it as an editor-ID.
        // KID has this same behavior (see `is_only_hex` in CLIBUtil).
        assert_eq!(
            Reference::parse("DEADBEEF"),
            Reference::FormIdOnly(0xDEADBEEF)
        );
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --lib reference::tests
cargo xwin check --package mora-kid --target x86_64-pc-windows-msvc
git add crates/mora-kid/src/reference.rs
git commit -m "mora-kid: Reference enum + KID-compatible parser

Reference::parse(str) dispatches on content: ~ (FormIdWithPlugin),
.es (PluginName), all-hex (FormIdOnly), else (EditorId).
resolve_form(world) maps to a FormId where possible (PluginName
returns None — plugin filters are non-form refs). 6 unit tests
including the all-hex-edid-vs-formid quirk that matches KID."
```

---

### Task 6: Reference resolution tests against EspWorld

**Files:**
- Create: `crates/mora-kid/tests/reference.rs`

- [ ] **Step 1: Write integration test**

```bash
mkdir -p crates/mora-kid/tests
cat > crates/mora-kid/tests/reference.rs <<'EOF'
//! Integration tests for Reference resolution against an EspWorld.

use std::io::Write;

use mora_core::FormId;
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::Reference;

fn write_tmp_plugin(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-ref-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path).unwrap().write_all(bytes).unwrap();
    path
}

fn build_keyword_plugin() -> Vec<u8> {
    // Minimal plugin with one KYWD record.
    let mut v = Vec::new();
    // TES4 header (ESM flag)
    v.extend_from_slice(b"TES4");
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());
    v.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    v.extend_from_slice(&1u32.to_le_bytes()); // ESM flag
    v.extend_from_slice(&0u32.to_le_bytes()); // form_id
    v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
    v.extend_from_slice(&44u16.to_le_bytes()); // version
    v.extend_from_slice(&0u16.to_le_bytes()); // unknown
    v.extend_from_slice(&tes4_body);
    // KYWD group
    let mut kywd_rec = Vec::new();
    kywd_rec.extend_from_slice(b"KYWD");
    let mut kywd_body = Vec::new();
    kywd_body.extend_from_slice(b"EDID");
    kywd_body.extend_from_slice(&16u16.to_le_bytes()); // NUL-terminated "WeapMaterialIron" = 16 bytes + NUL
    kywd_body.extend_from_slice(b"WeapMaterialIron\0");
    // Above was 17 bytes total (16 ascii + 1 NUL), so fix size:
    let edid_payload_len = kywd_body.len() - (4 + 2); // minus sig+size
    // Rewrite size correctly: the "16u16" above was wrong — rewrite in place.
    let size_offset = 4;
    let correct_size = (edid_payload_len as u16).to_le_bytes();
    kywd_body[size_offset..size_offset + 2].copy_from_slice(&correct_size);
    kywd_rec.extend_from_slice(&(kywd_body.len() as u32).to_le_bytes()); // data_size
    kywd_rec.extend_from_slice(&0u32.to_le_bytes()); // flags
    kywd_rec.extend_from_slice(&0x0001_E718u32.to_le_bytes()); // form_id
    kywd_rec.extend_from_slice(&0u32.to_le_bytes()); // vc_info
    kywd_rec.extend_from_slice(&44u16.to_le_bytes()); // version
    kywd_rec.extend_from_slice(&0u16.to_le_bytes()); // unknown
    kywd_rec.extend_from_slice(&kywd_body);
    // Group wrapping
    let mut grp = Vec::new();
    grp.extend_from_slice(b"GRUP");
    grp.extend_from_slice(&((24 + kywd_rec.len()) as u32).to_le_bytes()); // group_size
    grp.extend_from_slice(b"KYWD"); // label
    grp.extend_from_slice(&0u32.to_le_bytes()); // group_type = 0
    grp.extend_from_slice(&0u16.to_le_bytes()); // timestamp
    grp.extend_from_slice(&0u16.to_le_bytes()); // vc_info
    grp.extend_from_slice(&0u32.to_le_bytes()); // unknown
    grp.extend_from_slice(&kywd_rec);
    v.extend_from_slice(&grp);
    v
}

#[test]
fn resolve_editor_id_against_keyword_plugin() {
    let bytes = build_keyword_plugin();
    let path = write_tmp_plugin("KwRefPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-ref.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    let r = Reference::parse("WeapMaterialIron");
    let resolved = r.resolve_form(&world);
    assert_eq!(resolved, Some(FormId(0x0001_E718)));
}

#[test]
fn resolve_form_id_with_plugin_against_world() {
    let bytes = build_keyword_plugin();
    let path = write_tmp_plugin("KwFormPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-form.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    let r = Reference::parse("0x1E718~KwFormPlugin.esm");
    let resolved = r.resolve_form(&world);
    // plugin slot depends on load order; KwFormPlugin.esm should be slot 0x00.
    // FormId = 0x00 << 24 | (0x1E718 & 0xFFFFFF) = 0x0001E718
    assert_eq!(resolved, Some(FormId(0x0001_E718)));
}

#[test]
fn unknown_editor_id_returns_none() {
    let bytes = build_keyword_plugin();
    let path = write_tmp_plugin("KwUnknownPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-unknown.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    let r = Reference::parse("NonExistent");
    assert!(r.resolve_form(&world).is_none());
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --test reference
git add crates/mora-kid/tests/reference.rs
git commit -m "mora-kid: Reference resolution integration tests

3 tests against synthetic KYWD plugin: resolve editor-ID,
resolve FormIdWithPlugin, unknown editor-ID returns None."
```

---

## Phase E — Rule AST (Task 7)

### Task 7: Implement `rule.rs` — `KidRule` + `RecordType` + `FilterBuckets`

**Files:**
- Modify: `crates/mora-kid/src/rule.rs`

- [ ] **Step 1: Write rule.rs**

```bash
cat > crates/mora-kid/src/rule.rs <<'EOF'
//! KidRule AST.
//!
//! A `KidRule` captures one INI line after parsing. It is the input
//! to `KidDistributor::lower`.

use crate::reference::Reference;
use crate::traits_armor::ArmorTraits;
use crate::traits_weapon::WeaponTraits;

/// The record type a rule targets.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RecordType {
    Weapon,
    Armor,
    /// Any other type string — parsed but skipped at distribute time.
    Other(String),
}

impl RecordType {
    pub fn parse(s: &str) -> Self {
        match s.trim() {
            "Weapon" => RecordType::Weapon,
            "Armor" => RecordType::Armor,
            other => RecordType::Other(other.to_string()),
        }
    }
}

/// Type-specific trait bag. Only Weapon + Armor populated at M3.
#[derive(Debug, Clone, Default)]
pub enum Traits {
    #[default]
    None,
    Weapon(WeaponTraits),
    Armor(ArmorTraits),
}

/// Filter bucket: each entry is a Reference + optional plain-string
/// target (for string filters that didn't resolve as a form).
#[derive(Debug, Clone, Default)]
pub struct FilterBuckets {
    /// MATCH (OR) — at least one must match
    pub match_: Vec<Reference>,
    /// NOT — item must not match any of these
    pub not: Vec<Reference>,
    /// ALL (AND, `+` prefix) — all must match. Parsed at M3, evaluator
    /// at M3 LOGS-AND-SKIPS — activation is Plan 7+.
    pub all: Vec<Vec<Reference>>,
    /// ANY (`*` prefix, substring) — parsed but evaluator is Plan 7+.
    pub any: Vec<String>,
}

impl FilterBuckets {
    pub fn is_empty(&self) -> bool {
        self.match_.is_empty() && self.not.is_empty() && self.all.is_empty() && self.any.is_empty()
    }

    pub fn has_unsupported(&self) -> bool {
        !self.all.is_empty() || !self.any.is_empty()
    }
}

/// A parsed KID rule.
#[derive(Debug, Clone)]
pub struct KidRule {
    /// The keyword to distribute.
    pub keyword: Reference,
    /// Which record type the rule targets.
    pub record_type: RecordType,
    /// Filters (bucketed by prefix).
    pub filters: FilterBuckets,
    /// Record-type-specific trait filters.
    pub traits: Traits,
    /// Chance percentage (0..=100). 100 (always-pass) is the default.
    pub chance: u8,
    /// Source location for diagnostics.
    pub source: SourceLocation,
}

/// Where a rule came from. Used for error messages.
#[derive(Debug, Clone)]
pub struct SourceLocation {
    pub file: String,
    pub line_number: usize,
}

impl SourceLocation {
    pub const SYNTHETIC: SourceLocation = SourceLocation {
        file: String::new(),
        line_number: 0,
    };
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-kid --all-targets
git add crates/mora-kid/src/rule.rs
git commit -m "mora-kid: KidRule AST with RecordType, FilterBuckets, Traits

RecordType enum distinguishes Weapon/Armor/Other(string).
FilterBuckets carries MATCH/NOT (active) + ALL/ANY (parsed but
unsupported at M3, distributor logs-and-skips). Traits is a small
enum dispatching to per-type trait structs. SourceLocation tracked
for diagnostics."
```

---

## Phase F — Weapon + Armor trait parsers (Tasks 8-9)

### Task 8: `traits_weapon.rs`

**Files:**
- Modify: `crates/mora-kid/src/traits_weapon.rs`

- [ ] **Step 1: Write traits_weapon.rs**

```bash
cat > crates/mora-kid/src/traits_weapon.rs <<'EOF'
//! Weapon trait parser.
//!
//! Traits from KID's `WeaponTraits`:
//!   OneHandSword / OneHandDagger / OneHandAxe / OneHandMace
//!   TwoHandSword / TwoHandAxe / Bow / Crossbow / Staff / HandToHandMelee
//!   E / -E           (enchanted / not)
//!   T / -T           (template / no template)
//!   D(min max)       (damage range)
//!   W(min max)       (weight range)
//!
//! M3 parses these into a struct; the distributor uses them but
//! note that many require subrecord fields (DNAM for damage, OBND
//! for weight, etc.) that the current WeaponRecord does NOT expose.
//! Unsupported trait predicates log-and-skip at evaluate time.

use crate::TraitParseError;

/// Animation type (weapon type discriminator).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WeaponAnimType {
    OneHandSword,
    OneHandDagger,
    OneHandAxe,
    OneHandMace,
    TwoHandSword,
    TwoHandAxe,
    Bow,
    Crossbow,
    Staff,
    HandToHandMelee,
}

impl WeaponAnimType {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "OneHandSword" => Some(Self::OneHandSword),
            "OneHandDagger" => Some(Self::OneHandDagger),
            "OneHandAxe" => Some(Self::OneHandAxe),
            "OneHandMace" => Some(Self::OneHandMace),
            "TwoHandSword" => Some(Self::TwoHandSword),
            "TwoHandAxe" => Some(Self::TwoHandAxe),
            "Bow" => Some(Self::Bow),
            "Crossbow" => Some(Self::Crossbow),
            "Staff" => Some(Self::Staff),
            "HandToHandMelee" => Some(Self::HandToHandMelee),
            _ => None,
        }
    }
}

/// Parsed weapon trait filters. All fields default to "no constraint".
#[derive(Debug, Clone, Default)]
pub struct WeaponTraits {
    pub anim_types: Vec<WeaponAnimType>,
    pub require_enchanted: Option<bool>, // E (true), -E (false), None (no constraint)
    pub require_template: Option<bool>,  // T / -T
    pub damage_range: Option<(f32, f32)>,
    pub weight_range: Option<(f32, f32)>,
}

impl WeaponTraits {
    /// Parse a comma-separated traits string into a WeaponTraits.
    pub fn parse(s: &str) -> Result<Self, TraitParseError> {
        let mut out = WeaponTraits::default();
        if s.is_empty() || s.eq_ignore_ascii_case("NONE") {
            return Ok(out);
        }
        for token in s.split(',') {
            let t = token.trim();
            if t.is_empty() {
                continue;
            }
            if let Some(anim) = WeaponAnimType::parse(t) {
                out.anim_types.push(anim);
                continue;
            }
            match t {
                "E" => out.require_enchanted = Some(true),
                "-E" => out.require_enchanted = Some(false),
                "T" => out.require_template = Some(true),
                "-T" => out.require_template = Some(false),
                _ => {
                    if let Some(range) = parse_range(t, 'D') {
                        out.damage_range = Some(range);
                    } else if let Some(range) = parse_range(t, 'W') {
                        out.weight_range = Some(range);
                    } else {
                        return Err(TraitParseError::Unknown(t.to_string()));
                    }
                }
            }
        }
        Ok(out)
    }
}

/// Parse a range like `D(min max)` where `D` is the expected letter.
/// Returns None if the token doesn't match this form.
pub(crate) fn parse_range(s: &str, letter: char) -> Option<(f32, f32)> {
    let after_letter = s.strip_prefix(letter)?;
    let inner = after_letter.strip_prefix('(')?.strip_suffix(')')?;
    let mut parts = inner.split_whitespace();
    let min: f32 = parts.next()?.parse().ok()?;
    let max: f32 = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some((min, max))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_traits() {
        let t = WeaponTraits::parse("").unwrap();
        assert!(t.anim_types.is_empty());
        assert_eq!(t.require_enchanted, None);
    }

    #[test]
    fn none_is_empty() {
        let t = WeaponTraits::parse("NONE").unwrap();
        assert!(t.anim_types.is_empty());
    }

    #[test]
    fn anim_types() {
        let t = WeaponTraits::parse("OneHandSword,Bow").unwrap();
        assert_eq!(t.anim_types, vec![WeaponAnimType::OneHandSword, WeaponAnimType::Bow]);
    }

    #[test]
    fn enchantment_flags() {
        let t = WeaponTraits::parse("-E").unwrap();
        assert_eq!(t.require_enchanted, Some(false));
        let t = WeaponTraits::parse("E").unwrap();
        assert_eq!(t.require_enchanted, Some(true));
    }

    #[test]
    fn damage_range() {
        let t = WeaponTraits::parse("D(10 20)").unwrap();
        assert_eq!(t.damage_range, Some((10.0, 20.0)));
    }

    #[test]
    fn mixed() {
        let t = WeaponTraits::parse("Bow,-E,D(5 15)").unwrap();
        assert_eq!(t.anim_types, vec![WeaponAnimType::Bow]);
        assert_eq!(t.require_enchanted, Some(false));
        assert_eq!(t.damage_range, Some((5.0, 15.0)));
    }

    #[test]
    fn unknown_errors() {
        let err = WeaponTraits::parse("NotATrait").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
    }

    #[test]
    fn case_sensitive() {
        // KID: traits are case-sensitive via const_hash.
        // "onehandsword" (lowercase) is not recognized.
        let err = WeaponTraits::parse("onehandsword").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
    }
}
EOF
```

- [ ] **Step 2: Add `TraitParseError` to lib.rs**

The error type is shared across `traits_weapon` and `traits_armor`. Put it in `lib.rs` for convenience.

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/lib.rs")
text = p.read_text()
addendum = """
/// Error from parsing a trait token.
#[derive(Debug, thiserror::Error)]
pub enum TraitParseError {
    #[error("unknown trait: {0}")]
    Unknown(String),
}
"""
if "TraitParseError" not in text:
    text = text.rstrip() + "\n" + addendum
    p.write_text(text)
PY
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --lib traits_weapon::tests
git add crates/mora-kid/src/traits_weapon.rs crates/mora-kid/src/lib.rs
git commit -m "mora-kid: Weapon trait parser

WeaponAnimType enum (10 weapon types). WeaponTraits struct with
animation types, enchantment flag, template flag, damage range,
weight range. parse_range helper reads D(min max) / W(min max).
7 tests covering empty/NONE/animations/flags/range/mixed/unknown/case."
```

---

### Task 9: `traits_armor.rs`

**Files:**
- Modify: `crates/mora-kid/src/traits_armor.rs`

- [ ] **Step 1: Write traits_armor.rs**

```bash
cat > crates/mora-kid/src/traits_armor.rs <<'EOF'
//! Armor trait parser.
//!
//! Traits from KID's `ArmorTraits`:
//!   HEAVY / LIGHT / CLOTHING
//!   E / -E           (enchanted)
//!   T / -T           (templated)
//!   AR(min max)      (armor rating range)
//!   W(min max)       (weight range)
//!   30-61 (numeric)  (BipedObjectSlot)
//!
//! Same M3 caveat as weapon traits: predicates requiring subrecord
//! data not yet exposed on ArmorRecord log-and-skip at evaluate time.

use crate::traits_weapon::parse_range;
use crate::TraitParseError;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArmorType {
    Heavy,
    Light,
    Clothing,
}

impl ArmorType {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "HEAVY" => Some(Self::Heavy),
            "LIGHT" => Some(Self::Light),
            "CLOTHING" => Some(Self::Clothing),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct ArmorTraits {
    pub armor_types: Vec<ArmorType>,
    pub require_enchanted: Option<bool>,
    pub require_template: Option<bool>,
    pub ar_range: Option<(f32, f32)>,
    pub weight_range: Option<(f32, f32)>,
    pub body_slots: Vec<u8>, // 30..=61
}

impl ArmorTraits {
    pub fn parse(s: &str) -> Result<Self, TraitParseError> {
        let mut out = ArmorTraits::default();
        if s.is_empty() || s.eq_ignore_ascii_case("NONE") {
            return Ok(out);
        }
        for token in s.split(',') {
            let t = token.trim();
            if t.is_empty() {
                continue;
            }
            if let Some(at) = ArmorType::parse(t) {
                out.armor_types.push(at);
                continue;
            }
            match t {
                "E" => out.require_enchanted = Some(true),
                "-E" => out.require_enchanted = Some(false),
                "T" => out.require_template = Some(true),
                "-T" => out.require_template = Some(false),
                _ => {
                    if let Some(range) = parse_range(t, 'A') {
                        // KID uses AR(min max); our parse_range expects a single letter.
                        // Fall through: the helper only strips "A(", but KID's prefix is "AR".
                        // Use a dedicated parse_ar_range below.
                        let _ = range;
                        return Err(TraitParseError::Unknown(t.to_string()));
                    }
                    if let Some(range) = parse_ar_range(t) {
                        out.ar_range = Some(range);
                    } else if let Some(range) = parse_range(t, 'W') {
                        out.weight_range = Some(range);
                    } else if let Some(slot) = parse_body_slot(t) {
                        out.body_slots.push(slot);
                    } else {
                        return Err(TraitParseError::Unknown(t.to_string()));
                    }
                }
            }
        }
        Ok(out)
    }
}

fn parse_ar_range(s: &str) -> Option<(f32, f32)> {
    let after = s.strip_prefix("AR")?;
    let inner = after.strip_prefix('(')?.strip_suffix(')')?;
    let mut parts = inner.split_whitespace();
    let min: f32 = parts.next()?.parse().ok()?;
    let max: f32 = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some((min, max))
}

fn parse_body_slot(s: &str) -> Option<u8> {
    let n: u8 = s.parse().ok()?;
    if (30..=61).contains(&n) {
        Some(n)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_and_none() {
        assert!(ArmorTraits::parse("").unwrap().armor_types.is_empty());
        assert!(ArmorTraits::parse("NONE").unwrap().armor_types.is_empty());
    }

    #[test]
    fn armor_types() {
        let t = ArmorTraits::parse("HEAVY,LIGHT").unwrap();
        assert_eq!(t.armor_types, vec![ArmorType::Heavy, ArmorType::Light]);
    }

    #[test]
    fn ar_and_weight_ranges() {
        let t = ArmorTraits::parse("AR(20 100),W(5 15)").unwrap();
        assert_eq!(t.ar_range, Some((20.0, 100.0)));
        assert_eq!(t.weight_range, Some((5.0, 15.0)));
    }

    #[test]
    fn body_slots() {
        let t = ArmorTraits::parse("32,30,61").unwrap();
        assert_eq!(t.body_slots, vec![32, 30, 61]);
    }

    #[test]
    fn body_slot_out_of_range_is_error() {
        let err = ArmorTraits::parse("29").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
        let err = ArmorTraits::parse("62").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
    }

    #[test]
    fn mixed() {
        let t = ArmorTraits::parse("HEAVY,-E,32,AR(20 100)").unwrap();
        assert_eq!(t.armor_types, vec![ArmorType::Heavy]);
        assert_eq!(t.require_enchanted, Some(false));
        assert_eq!(t.body_slots, vec![32]);
        assert_eq!(t.ar_range, Some((20.0, 100.0)));
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --lib traits_armor::tests
git add crates/mora-kid/src/traits_armor.rs
git commit -m "mora-kid: Armor trait parser

ArmorType enum (Heavy/Light/Clothing). ArmorTraits struct with
armor types, enchantment flag, template flag, AR range, weight
range, body slots (30..=61). 6 tests."
```

---

## Phase G — INI line parser (Tasks 10-11)

### Task 10: Implement `ini.rs` — line parsing + file scanner

**Files:**
- Modify: `crates/mora-kid/src/ini.rs`

- [ ] **Step 1: Write ini.rs**

```bash
cat > crates/mora-kid/src/ini.rs <<'EOF'
//! KID INI file discovery + line parsing.
//!
//! See `docs/src/kid-ini-grammar.md` for the full grammar.

use std::path::{Path, PathBuf};

use tracing::warn;

use crate::filter::parse_filter_field;
use crate::reference::Reference;
use crate::rule::{FilterBuckets, KidRule, RecordType, SourceLocation, Traits};
use crate::traits_armor::ArmorTraits;
use crate::traits_weapon::WeaponTraits;
use crate::TraitParseError;

#[derive(Debug, thiserror::Error)]
pub enum IniError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("trait parse: {0}")]
    Trait(#[from] TraitParseError),
    #[error("{file}:{line}: missing type field")]
    MissingType { file: String, line: usize },
}

/// Discover all `*_KID.ini` files in a directory (non-recursive).
pub fn discover_kid_ini_files(data_dir: &Path) -> std::io::Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    for entry in std::fs::read_dir(data_dir)? {
        let entry = entry?;
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        let lower = name.to_ascii_lowercase();
        if lower.ends_with(".ini") && lower.contains("_kid") {
            out.push(path);
        }
    }
    out.sort();
    Ok(out)
}

/// Parse all rules in a KID INI file.
pub fn parse_file(path: &Path) -> Result<Vec<KidRule>, IniError> {
    let content = std::fs::read_to_string(path)?;
    let file_name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("<unknown>")
        .to_string();
    Ok(parse_ini_content(&content, &file_name))
}

/// Parse INI content (no I/O). Exposed for unit tests.
pub fn parse_ini_content(content: &str, file_name: &str) -> Vec<KidRule> {
    let mut rules = Vec::new();
    for (idx, raw) in content.lines().enumerate() {
        let line_number = idx + 1;
        let line = raw.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') || line.starts_with('[')
        {
            continue;
        }
        let Some(eq_pos) = line.find('=') else {
            continue;
        };
        let key = line[..eq_pos].trim();
        let value = line[eq_pos + 1..].trim();
        // KID reserves `ExclusiveGroup` — M3 ignores these lines.
        if key.eq_ignore_ascii_case("ExclusiveGroup") {
            continue;
        }
        match parse_rule_line(key, value, file_name, line_number) {
            Ok(rule) => rules.push(rule),
            Err(e) => {
                warn!("{file_name}:{line_number}: skipped rule: {e}");
            }
        }
    }
    rules
}

fn parse_rule_line(
    key: &str,
    value: &str,
    file_name: &str,
    line_number: usize,
) -> Result<KidRule, IniError> {
    let keyword = Reference::parse(key);
    let fields: Vec<&str> = value.split('|').collect();
    // Field 0: Type (required)
    let type_str = fields.first().map(|s| s.trim()).unwrap_or("");
    if type_str.is_empty() {
        return Err(IniError::MissingType {
            file: file_name.to_string(),
            line: line_number,
        });
    }
    let record_type = RecordType::parse(type_str);

    // Field 1: Filters (optional)
    let filters = match fields.get(1) {
        Some(s) if !is_absent(s) => parse_filter_field(s.trim()),
        _ => FilterBuckets::default(),
    };

    // Field 2: Traits (optional, type-specific)
    let traits = match fields.get(2) {
        Some(s) if !is_absent(s) => match &record_type {
            RecordType::Weapon => Traits::Weapon(WeaponTraits::parse(s.trim())?),
            RecordType::Armor => Traits::Armor(ArmorTraits::parse(s.trim())?),
            RecordType::Other(_) => Traits::None,
        },
        _ => Traits::None,
    };

    // Field 3: Chance (optional, default 100)
    let chance = match fields.get(3) {
        Some(s) if !is_absent(s) => {
            let v: f32 = s.trim().parse().unwrap_or(100.0);
            v.clamp(0.0, 100.0).round() as u8
        }
        _ => 100,
    };

    Ok(KidRule {
        keyword,
        record_type,
        filters,
        traits,
        chance,
        source: SourceLocation {
            file: file_name.to_string(),
            line_number,
        },
    })
}

fn is_absent(s: &str) -> bool {
    let t = s.trim();
    t.is_empty() || t.eq_ignore_ascii_case("NONE")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple_weapon_rule() {
        let content = "WeapMaterialIron = Weapon\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
        assert!(matches!(rules[0].record_type, RecordType::Weapon));
        assert!(matches!(rules[0].keyword, Reference::EditorId(ref s) if s == "WeapMaterialIron"));
        assert_eq!(rules[0].chance, 100);
    }

    #[test]
    fn skips_comments_blank_sections() {
        let content = "; a comment\n# another\n\n[IgnoreMe]\nWeapMaterialSteel = Weapon\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
    }

    #[test]
    fn skips_exclusive_group() {
        let content = "ExclusiveGroup = Materials|A,B,C\nWeapMaterialIron = Weapon\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
    }

    #[test]
    fn parses_chance() {
        let content = "WeapTypeBow = Weapon|||75\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
        assert_eq!(rules[0].chance, 75);
    }

    #[test]
    fn parses_armor_traits() {
        let content = "ArmorMaterialIron = Armor|||HEAVY,-E\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
        match &rules[0].traits {
            Traits::Armor(at) => {
                assert_eq!(at.armor_types.len(), 1);
                assert_eq!(at.require_enchanted, Some(false));
            }
            other => panic!("expected Armor traits, got {other:?}"),
        }
    }

    #[test]
    fn parses_weapon_with_filter_and_chance() {
        let content = "WeapTypeBow = Weapon|-E|Bow|50\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
        assert_eq!(rules[0].chance, 50);
        assert_eq!(rules[0].filters.not.len(), 1);
    }

    #[test]
    fn missing_type_is_skipped_with_warning() {
        let content = "NoType = \n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 0);
    }

    #[test]
    fn other_record_type_preserved() {
        let content = "AlchPoison = Potion|||-F\n";
        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
        assert!(matches!(rules[0].record_type, RecordType::Other(ref s) if s == "Potion"));
        // Traits for non-Weapon/Armor types are set to None.
        assert!(matches!(rules[0].traits, Traits::None));
    }
}
EOF
```

- [ ] **Step 2: Verify + commit** (after Task 11 implements `filter::parse_filter_field`)

Don't commit yet — compilation needs the filter module. Proceed to Task 11.

---

### Task 11: Implement `filter.rs` — filter bucket parsing + evaluation

**Files:**
- Modify: `crates/mora-kid/src/filter.rs`

- [ ] **Step 1: Write filter.rs**

```bash
cat > crates/mora-kid/src/filter.rs <<'EOF'
//! Filter-bucket parsing + evaluation.

use mora_core::FormId;
use mora_esp::EspWorld;

use crate::reference::Reference;
use crate::rule::FilterBuckets;

/// Parse a comma-separated filter field into bucketed `FilterBuckets`.
/// Dispatches each token on its prefix: `+` (ALL), `-` (NOT), `*` (ANY),
/// none (MATCH).
pub fn parse_filter_field(s: &str) -> FilterBuckets {
    let mut buckets = FilterBuckets::default();
    for token in s.split(',') {
        let t = token.trim();
        if t.is_empty() {
            continue;
        }
        if t.contains('+') {
            // ALL: split further on '+' into sub-references; all must match
            let parts: Vec<Reference> = t.split('+').map(|p| Reference::parse(p.trim())).collect();
            if !parts.is_empty() {
                buckets.all.push(parts);
            }
        } else if let Some(rest) = t.strip_prefix('-') {
            buckets.not.push(Reference::parse(rest.trim()));
        } else if let Some(rest) = t.strip_prefix('*') {
            buckets.any.push(rest.trim().to_string());
        } else {
            buckets.match_.push(Reference::parse(t));
        }
    }
    buckets
}

/// Evaluate the filter pipeline against an item. Returns `true` if the
/// item passes all active filters. `item_keywords` is the item's
/// existing keyword set (already resolved FormIDs). `item_plugin_index`
/// is the plugin index of the item's own source — used for plugin-name
/// filters.
///
/// M3 supports MATCH + NOT only. ALL + ANY log-and-skip (always pass
/// when present).
pub fn evaluate(
    buckets: &FilterBuckets,
    world: &EspWorld,
    item_plugin_index: usize,
    item_keywords: &[FormId],
) -> bool {
    // ALL bucket: at M3, non-empty ALL means "unsupported" — skip rule.
    // But we're an *evaluator*, not a parse step. Treat unsupported as
    // "no constraint" and emit a warning upstream in the distributor.
    // (The distributor scans rules and checks has_unsupported().)
    //
    // For parity with how KID would short-circuit: if ANY bucket is
    // non-empty in KID, that filter applies too. At M3 we skip — same
    // "fail open" policy.

    // NOT: if any matches, fail.
    for r in &buckets.not {
        if ref_matches_item(r, world, item_plugin_index, item_keywords) {
            return false;
        }
    }

    // MATCH: if bucket non-empty, at least one must match.
    if !buckets.match_.is_empty() {
        let any_matched = buckets
            .match_
            .iter()
            .any(|r| ref_matches_item(r, world, item_plugin_index, item_keywords));
        if !any_matched {
            return false;
        }
    }

    true
}

fn ref_matches_item(
    r: &Reference,
    world: &EspWorld,
    item_plugin_index: usize,
    item_keywords: &[FormId],
) -> bool {
    match r {
        Reference::PluginName(name) => {
            let Some(plugin) = world.plugins.get(item_plugin_index) else {
                return false;
            };
            plugin.filename.eq_ignore_ascii_case(name)
        }
        Reference::EditorId(_) | Reference::FormIdWithPlugin { .. } | Reference::FormIdOnly(_) => {
            // Resolve to a FormId. If the resolved form is a keyword
            // that the item has, match. (Form filters for non-keyword
            // references — e.g. matching the item *itself* — aren't
            // supported at M3; those would require per-record-type
            // identity checks.)
            let Some(fid) = r.resolve_form(world) else {
                return false;
            };
            item_keywords.contains(&fid)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_mixed_prefixes() {
        let buckets = parse_filter_field("A,-B,*C,D+E,F");
        assert_eq!(buckets.match_.len(), 2); // A and F
        assert_eq!(buckets.not.len(), 1); // B
        assert_eq!(buckets.any.len(), 1); // *C
        assert_eq!(buckets.all.len(), 1); // D+E
        assert_eq!(buckets.all[0].len(), 2);
    }

    #[test]
    fn none_returns_empty() {
        let buckets = parse_filter_field("NONE");
        // NONE is the literal string; our parser treats it as a single
        // MATCH token. Callers check is_absent before invoking.
        assert_eq!(buckets.match_.len(), 1);
    }

    #[test]
    fn empty_returns_empty() {
        let buckets = parse_filter_field("");
        assert!(buckets.is_empty());
    }
}
EOF
```

- [ ] **Step 2: Verify + commit (Tasks 10 and 11 together)**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --lib ini::tests
cargo test --package mora-kid --lib filter::tests
cargo xwin check --package mora-kid --target x86_64-pc-windows-msvc
git add crates/mora-kid/src/ini.rs crates/mora-kid/src/filter.rs
git commit -m "mora-kid: INI line parser + filter bucket dispatch + evaluator

ini::parse_file / parse_ini_content handle comments, sections,
exclusive-group skip, per-line rule parsing. filter::parse_filter_field
dispatches tokens to MATCH/NOT/ALL/ANY buckets by prefix.
filter::evaluate runs NOT + MATCH (ALL/ANY log-and-skip at M3,
activation in Plan 7). 10+ tests total."
```

---

## Phase H — KidDistributor (Tasks 12-13)

### Task 12: Implement `distributor.rs`

**Files:**
- Modify: `crates/mora-kid/src/distributor.rs`

- [ ] **Step 1: Write distributor.rs**

```bash
cat > crates/mora-kid/src/distributor.rs <<'EOF'
//! KidDistributor — impl of `mora_core::Distributor<EspWorld>`.
//!
//! Scans Weapon + Armor records in the world, evaluates each rule's
//! filter pipeline against the record, runs the deterministic chance
//! roll, emits `Patch::AddKeyword` patches to the sink.

use mora_core::{DeterministicChance, Distributor, DistributorStats, FormId, Patch, PatchSink};
use mora_esp::EspWorld;
use tracing::{debug, warn};

use crate::filter;
use crate::rule::{KidRule, RecordType, Traits};

#[derive(Debug, thiserror::Error)]
pub enum KidError {
    #[error("keyword reference did not resolve: {0:?}")]
    UnresolvedKeyword(crate::reference::Reference),
}

/// KidDistributor — consumes a list of parsed rules + an EspWorld,
/// emits patches.
pub struct KidDistributor {
    pub rules: Vec<KidRule>,
}

impl KidDistributor {
    pub fn new(rules: Vec<KidRule>) -> Self {
        KidDistributor { rules }
    }
}

impl Distributor<EspWorld> for KidDistributor {
    type Error = KidError;

    fn name(&self) -> &'static str {
        "kid"
    }

    fn lower(
        &self,
        world: &EspWorld,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error> {
        let mut stats = DistributorStats::default();
        stats.rules_evaluated = self.rules.len() as u64;

        // Pre-resolve each rule's keyword FormId + editor-ID string.
        struct ResolvedRule<'a> {
            rule: &'a KidRule,
            keyword_form_id: FormId,
            keyword_editor_id: String,
        }
        let mut resolved: Vec<ResolvedRule<'_>> = Vec::new();
        for rule in &self.rules {
            let Some(fid) = rule.keyword.resolve_form(world) else {
                warn!(
                    "{}:{}: keyword {:?} did not resolve — rule skipped",
                    rule.source.file, rule.source.line_number, rule.keyword
                );
                continue;
            };
            // Resolve the editor-id for chance seeding. If the rule key
            // was already an EditorId, use that string directly; else
            // look it up via the world.
            let edid = match &rule.keyword {
                crate::reference::Reference::EditorId(s) => s.clone(),
                _ => {
                    // Find the editor-id of this keyword FormId.
                    let mut out = None;
                    for entry in world.keywords() {
                        if let Ok((entry_fid, kw)) = entry
                            && entry_fid == fid
                            && let Some(edid) = kw.editor_id.clone()
                        {
                            out = Some(edid);
                            break;
                        }
                    }
                    match out {
                        Some(s) => s,
                        None => {
                            warn!(
                                "{}:{}: keyword {:?} resolved to {fid} but editor-id not found",
                                rule.source.file, rule.source.line_number, rule.keyword
                            );
                            continue;
                        }
                    }
                }
            };
            if rule.filters.has_unsupported() {
                debug!(
                    "{}:{}: rule has unsupported ALL/ANY filters; evaluator treats as pass",
                    rule.source.file, rule.source.line_number
                );
            }
            resolved.push(ResolvedRule {
                rule,
                keyword_form_id: fid,
                keyword_editor_id: edid,
            });
        }

        // Iterate weapons via world.records(WEAP) so we get the exact
        // source plugin_index — world.weapons() drops it, and
        // reconstructing it from the resolved FormId's high byte is
        // unreliable for ESL plugins (which all share 0xFE).
        for wr in world.records(mora_esp::signature::WEAP) {
            let form_id = wr.resolved_form_id;
            let plugin_index = wr.plugin_index;
            let weapon = match mora_esp::records::weapon::parse(&wr.record, plugin_index, world) {
                Ok(w) => w,
                Err(_) => continue,
            };
            stats.candidates_considered += 1;

            for rr in &resolved {
                if !matches!(rr.rule.record_type, RecordType::Weapon) {
                    continue;
                }
                if !filter::evaluate(&rr.rule.filters, world, plugin_index, &weapon.keywords) {
                    stats.rejected_by_filter += 1;
                    continue;
                }
                // M3: trait evaluation log-and-skip (Weapon trait predicates
                // require DNAM/OBND subrecord data not yet on WeaponRecord).
                if let Traits::Weapon(ref wt) = &rr.rule.traits
                    && !wt.anim_types.is_empty()
                {
                    debug!(
                        "{}:{}: weapon trait predicates not yet evaluated (WeaponRecord lacks animType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }
                // Chance roll.
                if !chance.passes(&rr.keyword_editor_id, form_id, rr.rule.chance) {
                    stats.rejected_by_chance += 1;
                    continue;
                }
                sink.push(Patch::AddKeyword {
                    target: form_id,
                    keyword: rr.keyword_form_id,
                });
                stats.patches_emitted += 1;
            }
        }

        // Iterate armors (same structure).
        for wr in world.records(mora_esp::signature::ARMO) {
            let form_id = wr.resolved_form_id;
            let plugin_index = wr.plugin_index;
            let armor = match mora_esp::records::armor::parse(&wr.record, plugin_index, world) {
                Ok(a) => a,
                Err(_) => continue,
            };
            stats.candidates_considered += 1;
            for rr in &resolved {
                if !matches!(rr.rule.record_type, RecordType::Armor) {
                    continue;
                }
                if !filter::evaluate(&rr.rule.filters, world, plugin_index, &armor.keywords) {
                    stats.rejected_by_filter += 1;
                    continue;
                }
                if let Traits::Armor(ref at) = &rr.rule.traits
                    && !at.armor_types.is_empty()
                {
                    debug!(
                        "{}:{}: armor trait predicates not yet evaluated (ArmorRecord lacks armorType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }
                if !chance.passes(&rr.keyword_editor_id, form_id, rr.rule.chance) {
                    stats.rejected_by_chance += 1;
                    continue;
                }
                sink.push(Patch::AddKeyword {
                    target: form_id,
                    keyword: rr.keyword_form_id,
                });
                stats.patches_emitted += 1;
            }
        }

        // Warn for Other record types.
        for rr in &resolved {
            if let RecordType::Other(ref t) = rr.rule.record_type {
                warn!(
                    "{}:{}: record type {:?} not supported at M3 (Weapon+Armor only)",
                    rr.rule.source.file, rr.rule.source.line_number, t
                );
            }
        }

        Ok(stats)
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-kid --all-targets
cargo xwin check --package mora-kid --target x86_64-pc-windows-msvc
git add crates/mora-kid/src/distributor.rs
git commit -m "mora-kid: KidDistributor — impl Distributor<EspWorld>

Scans weapons + armors in the world, evaluates each rule's filter
pipeline (NOT + MATCH active; ALL/ANY log-and-skip), runs
KID-compatible deterministic chance roll seeded by
(keyword_editor_id, form_id), emits Patch::AddKeyword.
Unresolved keyword refs / unsupported record types log and skip.
Stats accumulated per rule/candidate."
```

---

### Task 13: Implement `pipeline.rs` — top-level compile entry

**Files:**
- Modify: `crates/mora-kid/src/pipeline.rs`

- [ ] **Step 1: Write pipeline.rs**

```bash
cat > crates/mora-kid/src/pipeline.rs <<'EOF'
//! Top-level compile pipeline. Combines INI discovery, parsing,
//! and distribution into one `compile()` call.

use std::path::Path;

use mora_core::{DeterministicChance, Distributor, Patch, PatchSink};
use mora_esp::EspWorld;

use crate::distributor::{KidDistributor, KidError};
use crate::ini::{self, IniError};

#[derive(Debug, thiserror::Error)]
pub enum CompileError {
    #[error("ini: {0}")]
    Ini(#[from] IniError),
    #[error("kid: {0}")]
    Kid(#[from] KidError),
}

/// Compile: discover all `*_KID.ini` in the data dir, parse rules,
/// run the distributor against `world`, return the finalized
/// `Vec<Patch>`.
///
/// `data_dir` is scanned for INI files; `world` supplies the loaded
/// plugins.
pub fn compile(
    data_dir: &Path,
    world: &EspWorld,
) -> Result<Vec<Patch>, CompileError> {
    let ini_paths = ini::discover_kid_ini_files(data_dir)?;
    let mut all_rules = Vec::new();
    for p in &ini_paths {
        let rules = ini::parse_file(p)?;
        all_rules.extend(rules);
    }

    let distributor = KidDistributor::new(all_rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    distributor.lower(world, &chance, &mut sink)?;
    let file = sink.finalize();
    Ok(file.patches)
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-kid/src/pipeline.rs
git commit -m "mora-kid: pipeline::compile — top-level entry

discover *_KID.ini in data_dir, parse all rules, build
KidDistributor, run against world with KID-compatible chance,
return finalized Vec<Patch>. This is the public entry that Plan 7's
mora-cli will call to produce mora_patches.bin."
```

---

## Phase I — Integration tests (Tasks 14-15)

### Task 14: INI parsing integration test

**Files:**
- Create: `crates/mora-kid/tests/parse.rs`

- [ ] **Step 1: Write parse.rs**

```bash
cat > crates/mora-kid/tests/parse.rs <<'EOF'
//! Integration tests for KID INI parsing.

use mora_kid::ini::parse_ini_content;
use mora_kid::rule::{RecordType, Traits};

#[test]
fn parses_real_world_style_kid_ini() {
    let content = r"; A real-world-ish KID INI file
# Another comment

[ignored section]

; Simple weapon rule
WeapMaterialIron = Weapon

; Armor with traits + chance
ArmorMaterialSteel = Armor|||HEAVY|50

; Weapon with filter and chance
WeapTypeBow = Weapon|-E|Bow|75

; Unsupported record type (parsed but distributor skips)
AlchPoison = Potion|||-F

; Exclusive group — skipped
ExclusiveGroup = MaterialGroup|WeapMaterialIron,WeapMaterialSteel

; Missing type — skipped with warning
NoTypeHere =
";

    let rules = parse_ini_content(content, "test.ini");
    assert_eq!(rules.len(), 4); // the 4 well-formed rules

    assert!(matches!(rules[0].record_type, RecordType::Weapon));
    assert_eq!(rules[0].chance, 100);

    assert!(matches!(rules[1].record_type, RecordType::Armor));
    assert_eq!(rules[1].chance, 50);
    assert!(matches!(rules[1].traits, Traits::Armor(_)));

    assert!(matches!(rules[2].record_type, RecordType::Weapon));
    assert_eq!(rules[2].chance, 75);
    assert_eq!(rules[2].filters.not.len(), 1);

    assert!(matches!(rules[3].record_type, RecordType::Other(ref s) if s == "Potion"));
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --test parse
git add crates/mora-kid/tests/parse.rs
git commit -m "mora-kid: INI parsing integration test

One integration test exercising comments, sections, simple/chance/
traits/filters rules, exclusive-group skip, unsupported-type, and
missing-type-skipped-with-warning."
```

---

### Task 15: KidDistributor end-to-end test against synthetic ESP

**Files:**
- Create: `crates/mora-kid/tests/distribute.rs`

- [ ] **Step 1: Write distribute.rs**

This test builds a real EspWorld with a plugin containing WEAP, ARMO, and KYWD records, then runs a KidDistributor against a small rule set, then asserts on the emitted patches.

```bash
cat > crates/mora-kid/tests/distribute.rs <<'EOF'
//! End-to-end test: KidDistributor against a synthetic EspWorld.

use std::io::Write;

use mora_core::{DeterministicChance, Distributor, FormId, Patch, PatchSink};
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_ini_content;

fn write_tmp(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-dist-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path).unwrap().write_all(bytes).unwrap();
    path
}

// Helper to build a minimal synthetic plugin with one KYWD, one WEAP, one ARMO.
fn build_plugin_bytes() -> Vec<u8> {
    fn sub(sig: &[u8; 4], data: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(sig);
        v.extend_from_slice(&(data.len() as u16).to_le_bytes());
        v.extend_from_slice(data);
        v
    }
    fn rec(sig: &[u8; 4], form_id: u32, body: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(sig);
        v.extend_from_slice(&(body.len() as u32).to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // flags
        v.extend_from_slice(&form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&44u16.to_le_bytes()); // version
        v.extend_from_slice(&0u16.to_le_bytes()); // unknown
        v.extend_from_slice(body);
        v
    }
    fn group(label: &[u8; 4], contents: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&((24 + contents.len()) as u32).to_le_bytes());
        v.extend_from_slice(label);
        v.extend_from_slice(&0u32.to_le_bytes()); // group_type=0
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(contents);
        v
    }
    fn nul_cstr(s: &str) -> Vec<u8> {
        let mut v = s.as_bytes().to_vec();
        v.push(0);
        v
    }

    // TES4
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    // TES4 record
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes()); // ESM flag
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);

    // KYWD group: one keyword "WeapMaterialIron" @ FormID 0x0001_E718
    let kwbody = sub(b"EDID", &nul_cstr("WeapMaterialIron"));
    let kwrec = rec(b"KYWD", 0x0001_E718, &kwbody);
    out.extend_from_slice(&group(b"KYWD", &kwrec));

    // WEAP group: one weapon "IronSword" @ 0x0001_2EB7 with no keywords
    let mut wbody = sub(b"EDID", &nul_cstr("IronSword"));
    wbody.extend_from_slice(&sub(b"KWDA", &[])); // empty keyword list
    let wrec = rec(b"WEAP", 0x0001_2EB7, &wbody);
    out.extend_from_slice(&group(b"WEAP", &wrec));

    // ARMO group: one armor "IronHelmet" @ 0x0001_CCCC with no keywords
    let mut abody = sub(b"EDID", &nul_cstr("IronHelmet"));
    abody.extend_from_slice(&sub(b"KWDA", &[])); // empty keyword list
    let arec = rec(b"ARMO", 0x0001_CCCC, &abody);
    out.extend_from_slice(&group(b"ARMO", &arec));

    out
}

fn open_world() -> (EspWorld, std::path::PathBuf) {
    let bytes = build_plugin_bytes();
    let path = write_tmp("KidDistPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let data_dir = path.parent().unwrap();
    let plugins_txt = data_dir.join("plugins-kid.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(data_dir, &plugins_txt).unwrap();
    (world, data_dir.to_path_buf())
}

#[test]
fn distributes_keyword_to_weapon() {
    let (world, _) = open_world();

    let rules = parse_ini_content(
        "WeapMaterialIron = Weapon\n",
        "test.ini",
    );
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    let stats = dist.lower(&world, &chance, &mut sink).unwrap();

    assert_eq!(stats.patches_emitted, 1);
    let file = sink.finalize();
    assert_eq!(file.patches.len(), 1);
    assert!(matches!(
        file.patches[0],
        Patch::AddKeyword {
            target: FormId(0x0001_2EB7),
            keyword: FormId(0x0001_E718)
        }
    ));
}

#[test]
fn distributes_keyword_to_armor() {
    let (world, _) = open_world();
    let rules = parse_ini_content(
        "WeapMaterialIron = Armor\n",
        "test.ini",
    );
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    let stats = dist.lower(&world, &chance, &mut sink).unwrap();

    assert_eq!(stats.patches_emitted, 1);
    let file = sink.finalize();
    assert!(matches!(
        file.patches[0],
        Patch::AddKeyword {
            target: FormId(0x0001_CCCC),
            keyword: FormId(0x0001_E718)
        }
    ));
}

#[test]
fn unresolved_keyword_skips_rule() {
    let (world, _) = open_world();
    let rules = parse_ini_content("NonExistentKeyword = Weapon\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(&world, &chance, &mut sink).unwrap();
    assert_eq!(sink.len(), 0);
}

#[test]
fn unsupported_record_type_skips_rule() {
    let (world, _) = open_world();
    let rules = parse_ini_content("WeapMaterialIron = Potion\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(&world, &chance, &mut sink).unwrap();
    assert_eq!(sink.len(), 0);
}

#[test]
fn chance_zero_never_emits() {
    let (world, _) = open_world();
    let rules = parse_ini_content("WeapMaterialIron = Weapon|||0\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(&world, &chance, &mut sink).unwrap();
    assert_eq!(sink.len(), 0);
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --test distribute
git add crates/mora-kid/tests/distribute.rs
git commit -m "mora-kid: end-to-end distribution integration tests

5 tests against synthetic ESP with 1 KYWD + 1 WEAP + 1 ARMO:
  - distributes to weapon
  - distributes to armor
  - unresolved keyword -> rule skipped
  - unsupported record type -> rule skipped
  - chance=0 -> nothing emitted
Exercises full mora-kid pipeline end-to-end with real mora-esp."
```

---

## Phase J — Final verification (Task 16)

### Task 16: Full clean verify + push + PR

**Files:** none modified.

- [ ] **Step 1: Clean verification**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets 2>&1 | grep -E "^test result" | awk '{count+=$4} END {print "TOTAL:", count}'
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all six green. Test count: M2's 126 + ~25 new mora-kid tests = ~151.

- [ ] **Step 2: Push + open PR**

```bash
git push -u origin m3-mora-kid-mvp
gh pr create --base master --head m3-mora-kid-mvp \
    --title "Rust + KID pivot — M3: mora-kid MVP" \
    --body "$(cat <<'PRBODY'
## Summary

Delivers `mora-kid` library — KID INI parser + `KidDistributor`
implementing `Distributor<EspWorld>`. With this, Mora can (given a
pre-opened EspWorld) parse KID INI files and produce a `Vec<Patch>`
ready for runtime consumption.

- **Reference types** — KID's 4-way dispatch (editor-ID / FormID~Plugin / plugin-only / bare hex) with `resolve_form(world)` helper
- **`KidRule` AST** — keyword, RecordType, FilterBuckets, Traits, chance, source location
- **Filter bucket dispatch** — `+` ALL, `-` NOT, `*` ANY, none MATCH (M3: NOT+MATCH active; ALL/ANY log-and-skip, Plan 7 activates)
- **Weapon + Armor trait parsers** — full per-type grammar from KID's `WeaponTraits` / `ArmorTraits`
- **INI line parser** — comments, sections, ExclusiveGroup-skip, chance default, NONE sentinel
- **`KidDistributor`** — scans world.weapons() + world.armors(), evaluates filters, KID-bit-compatible chance roll, emits Patch::AddKeyword
- **`pipeline::compile(data_dir, world)`** — top-level entry; Plan 7's mora-cli calls this
- **mora-esp extension** — KYWD record type + `EspWorld::keywords()` + `resolve_keyword_by_editor_id()` so editor-ID references resolve to FormIds
- `docs/src/kid-ini-grammar.md` — grammar reference derived from KID source

## Test plan

- [x] `cargo test --workspace` — ~151 total tests (~126 prior + ~25 new)
- [x] `cargo clippy --all-targets -- -D warnings` clean
- [x] `cargo fmt --check` clean
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean
- [x] End-to-end synthetic integration test: 1-plugin world with KYWD+WEAP+ARMO, KidDistributor emits AddKeyword patches as expected

## Scope discipline

- **Record types at M3:** Weapon + Armor only. Other types parsed but skipped.
- **Filter buckets at M3:** NOT + MATCH active; ALL (`+`) and ANY (`*`) parsed into AST but evaluator treats as pass (Plan 7+ activates).
- **ExclusiveGroup:** parsed-as-key-match and ignored (Plan 7+ implements).
- **Trait predicates:** parsed but some require subrecord fields not yet on WeaponRecord/ArmorRecord (anim type, armor type, DNAM). Those log-and-treat-as-pass — mora-esp extensions in Plan 8 activate.
- **No CLI wire-up.** That's Plan 7.

## Next up

**Plan 7: mora-cli end-to-end** — wire `pipeline::compile` into the `mora compile` subcommand, produce the first real `mora_patches.bin`, set up the output directory + install flow.
PRBODY
)"
```

- [ ] **Step 3: Watch CI + hand off**

```bash
gh run watch --exit-status 2>&1 | tail -8
```

---

## Completion criteria

- [ ] ~25 new tests pass (reference/traits/ini/filter/distribute).
- [ ] `cargo clippy -D warnings` clean.
- [ ] `pipeline::compile(data_dir, world)` produces a `Vec<Patch>` from a real Data/ dir + EspWorld.
- [ ] PR merged to `master`.

## Next plan

**Plan 7: mora-cli end-to-end** — mora compile subcommand (auto-detect Skyrim install, load plugins.txt, open EspWorld, call pipeline::compile, write mora_patches.bin). First real user-facing `mora compile`.
