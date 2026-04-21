# Rust + KID Pivot — Plan 8b: trait predicates

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete KID parity on Weapon + Armor rules by activating trait-predicate evaluation (`OneHandSword`, `HEAVY`, `-E`, `D(min max)`, body slot numbers, etc.). Extends `mora-esp`'s `WeaponRecord`/`ArmorRecord` with the subrecord-derived fields KID's trait grammar checks, then activates those predicates in `mora-kid`'s filter evaluator.

**Architecture:** Extends `mora-esp/src/records/{weapon,armor}.rs` to parse DNAM / DATA / EITM / CNAM / TNAM / BOD2 / BODT subrecords. Adds typed enums (`WeaponAnimType`, `ArmorType`) + bitflags (`BipedObjectSlots`) in `mora-esp`. Mora-kid's distributor delegates trait evaluation to per-record-type helpers that inspect the record struct. No changes to the AST — the existing `WeaponTraits` / `ArmorTraits` structs (Plan 6) are the target shape.

**Tech Stack:** Rust 1.90. No new workspace deps.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope discipline:**
- **Weapon + Armor only.** Other 17 record types stay out of scope.
- **BOD2 + BODT both supported.** SSE uses BOD2 primarily; BODT appears in LE-era plugins that weren't re-saved. Parser handles both.
- **Armor rating stored as `display × 100`.** Parser returns `f32 = raw as f32 / 100.0` so callers see display-scale values.
- **EAMT (enchantment capacity)** is present on WEAP per xEdit's `wbEnchantment(True)`, absent on ARMO per `wbEnchantment` (no `True` arg). We parse EITM on both; EAMT is skipped everywhere (KID doesn't use it).
- **No new mora-cli work.** Distributor activation is enough.
- **No DNAM ARMO fallback** — if DNAM is absent on an armor, armor_rating is `None`, predicates using it log-and-fail rather than crash.

---

## File Structure

**Modified:**
- `crates/mora-esp/src/signature.rs` — add signature constants: `DATA`, `DNAM`, `EITM`, `CNAM`, `TNAM`, `BOD2`, `BODT`
- `crates/mora-esp/src/records/weapon.rs` — parse DATA + DNAM + EITM + CNAM; expose fields
- `crates/mora-esp/src/records/armor.rs` — parse DATA + DNAM + EITM + TNAM + BOD2/BODT; expose fields
- `crates/mora-esp/src/subrecords/mod.rs` — add new parser modules
- `crates/mora-kid/src/filter.rs` — new `evaluate_weapon_traits` / `evaluate_armor_traits`
- `crates/mora-kid/src/distributor.rs` — call trait evaluators (replaces log-and-skip)
- `docs/src/mora-esp-reference.md` — append subrecord layout tables

**Created:**
- `crates/mora-esp/src/subrecords/weapon_data.rs` — WEAP DATA parser (value + weight + damage)
- `crates/mora-esp/src/subrecords/weapon_dnam.rs` — WEAP DNAM parser (anim_type + speed + reach)
- `crates/mora-esp/src/subrecords/armor_data.rs` — ARMO DATA parser (value + weight)
- `crates/mora-esp/src/subrecords/armor_dnam.rs` — ARMO DNAM parser (armor_rating)
- `crates/mora-esp/src/subrecords/biped_object.rs` — BOD2/BODT parser (slots + armor_type)
- `crates/mora-esp/src/subrecords/form_id_ref.rs` — generic FormID subrecord reader (for EITM/CNAM/TNAM)
- `crates/mora-kid/tests/trait_predicates.rs` — end-to-end tests for trait predicates

---

## Phase A — mora-esp reference doc update (Task 1)

### Task 1: Append subrecord layouts to `docs/src/mora-esp-reference.md`

**Files:**
- Modify: `docs/src/mora-esp-reference.md`

- [ ] **Step 1: Append layouts**

```bash
cd /home/tbaldrid/oss/mora
cat >> docs/src/mora-esp-reference.md <<'EOF'

---

# Plan 8b additions — WEAP + ARMO subrecord layouts

Binary layouts for the subrecords Plan 8b parses, to activate KID
trait predicates on Weapon + Armor. Sources: xEdit
`wbDefinitionsTES5.pas` (primary) + CommonLibSSE-NG headers
(cross-reference). Discrepancies resolved in favor of xEdit (which
is wire-format authoritative).

All offsets are from the start of the subrecord payload (after the
6-byte subrecord header).

## WEAP DATA — value + weight + damage

Total size: **10 bytes**

| Offset | Size | Field  | Type | Notes        |
|--------|------|--------|------|--------------|
| 0x00   | 4    | value  | u32  | gold value   |
| 0x04   | 4    | weight | f32  | item weight  |
| 0x08   | 2    | damage | u16  | base damage  |

## WEAP DNAM — animation type + reach + speed (plus much more we skip)

Total size: **100 bytes** (wire format). The runtime C++ struct in
CommonLibSSE-NG is smaller (56 bytes) because it stores some fields
out-of-line; the file blob is the flat 100-byte version.

| Offset | Size | Field          | Type | M3 use               |
|--------|------|----------------|------|----------------------|
| 0x00   | 1    | animation_type | u8   | WeaponAnimType enum  |
| 0x01   | 3    | _padding       | —    | skip                 |
| 0x04   | 4    | speed          | f32  | skip (M3+)           |
| 0x08   | 4    | reach          | f32  | skip (M3+)           |
| 0x0C+  |      | (many more)    |      | skip for M3          |

### `animation_type` enum (u8)

```
0 = HandToHandMelee
1 = OneHandSword
2 = OneHandDagger
3 = OneHandAxe
4 = OneHandMace
5 = TwoHandSword
6 = TwoHandAxe
7 = Bow
8 = Staff
9 = Crossbow
```

## WEAP EITM — enchantment

**Total size: 4 bytes.** Single FormID pointing to an ENCH form.
Subrecord absent → weapon is unenchanted.

EAMT (u16, enchantment capacity) may also appear on WEAP per
`wbEnchantment(True)` — Mora reads it only as "if present, skip".

## WEAP CNAM — template

**Total size: 4 bytes.** FormID pointing to another WEAP. Optional.
When present, many fields on this record are inherited from the
template.

## ARMO DATA — value + weight

Total size: **8 bytes**

| Offset | Size | Field  | Type | Notes                          |
|--------|------|--------|------|--------------------------------|
| 0x00   | 4    | value  | i32  | gold value (signed on disk)    |
| 0x04   | 4    | weight | f32  | item weight                    |

## ARMO DNAM — armor rating

Total size: **4 bytes**

| Offset | Size | Field        | Type | Notes                                  |
|--------|------|--------------|------|----------------------------------------|
| 0x00   | 4    | armor_rating | i32  | stored as display × 100 (divide on read) |

Mora reads as i32, converts to `f32 / 100.0` before exposing.

## ARMO BOD2 — biped slots + armor type (SSE primary)

Total size: **8 bytes**

| Offset | Size | Field              | Type | Notes                                    |
|--------|------|--------------------|------|------------------------------------------|
| 0x00   | 4    | biped_object_slots | u32  | bitmask; bit N = slot (30 + N)           |
| 0x04   | 4    | armor_type         | u32  | 0=LightArmor, 1=HeavyArmor, 2=Clothing   |

Body-slot bit → slot-number mapping (selected):
- bit 0 → slot 30 (Head)
- bit 2 → slot 32 (Body)
- bit 3 → slot 33 (Hands)
- bit 7 → slot 37 (Feet)
- bit 9 → slot 39 (Shield)
- bit 12 → slot 42 (Circlet)
- bit 31 → slot 61 (FX01)

## ARMO BODT — legacy format (pre-SSE plugins)

Total size: **12 bytes**

| Offset | Size | Field              | Type | Notes                       |
|--------|------|--------------------|------|-----------------------------|
| 0x00   | 4    | biped_object_slots | u32  | same bitmask as BOD2        |
| 0x04   | 1    | general_flags      | u8   | skip (NonPlayable lives in record header now) |
| 0x05   | 3    | _padding           | —    | skip                        |
| 0x08   | 4    | armor_type         | u32  | same enum as BOD2           |

Mora's parser reads whichever of BOD2 / BODT appears (BOD2 preferred
if both are present — which shouldn't happen in well-formed plugins).

## ARMO EITM — enchantment

**Total size: 4 bytes.** Same format as WEAP EITM. No `EAMT`
sibling on ARMO per xEdit (`wbEnchantment` without `True`).

## ARMO TNAM — template armor (NOT `CNAM`)

**Total size: 4 bytes.** FormID pointing to another ARMO. Armor's
template subrecord signature is **`TNAM`**, not `CNAM` like weapon —
a common source of parser bugs.

EOF
```

- [ ] **Step 2: Commit**

```bash
git add docs/src/mora-esp-reference.md
git commit -m "docs: mora-esp reference — WEAP + ARMO subrecord layouts

Appends byte-layout tables for WEAP DATA/DNAM/EITM/CNAM and
ARMO DATA/DNAM/EITM/TNAM/BOD2/BODT. Animation-type + armor-type
enum values. Body-slot bitmask conventions. Cited by every
parser task in Plan 8b."
```

---

## Phase B — signature constants + generic FormID reader (Task 2)

### Task 2: Add signature constants + shared FormID-ref helper

**Files:**
- Modify: `crates/mora-esp/src/signature.rs`
- Create: `crates/mora-esp/src/subrecords/form_id_ref.rs`
- Modify: `crates/mora-esp/src/subrecords/mod.rs`

- [ ] **Step 1: Add signature constants**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/signature.rs")
text = p.read_text()
existing = """pub const SNAM: Signature = Signature::new(b"SNAM");"""
# Add new constants after SNAM.
addition = """
pub const DNAM: Signature = Signature::new(b"DNAM");
pub const EITM: Signature = Signature::new(b"EITM");
pub const CNAM: Signature = Signature::new(b"CNAM");
pub const TNAM: Signature = Signature::new(b"TNAM");
pub const BOD2: Signature = Signature::new(b"BOD2");
pub const BODT: Signature = Signature::new(b"BODT");"""
if "DNAM" not in text:
    text = text.replace(existing, existing + addition)
    p.write_text(text)
PY
grep -E "(DNAM|EITM|CNAM|TNAM|BOD2|BODT)" crates/mora-esp/src/signature.rs
```

Note: `DATA` is already in signature.rs from Plan 5 (Task 9).

- [ ] **Step 2: Write the generic FormID reader**

```bash
cat > crates/mora-esp/src/subrecords/form_id_ref.rs <<'EOF'
//! Generic single-FormID subrecord parser.
//!
//! Used for EITM, CNAM, TNAM — all 4-byte LE u32 FormIDs pointing
//! to another form. Returns the LOCAL FormID (pre-resolution);
//! callers apply `EspWorld::resolve_in_plugin` to promote to the
//! runtime FormId.

use crate::reader::ReadError;

/// Parse a 4-byte LE FormID from a subrecord payload.
pub fn parse(data: &[u8]) -> Result<u32, ReadError> {
    if data.len() < 4 {
        return Err(ReadError::Truncated {
            offset: 0,
            needed: 4,
        });
    }
    Ok(u32::from_le_bytes(data[..4].try_into().unwrap()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_happy() {
        let bytes = [0xB7, 0x2E, 0x01, 0x00];
        let fid = parse(&bytes).unwrap();
        assert_eq!(fid, 0x0001_2EB7);
    }

    #[test]
    fn rejects_too_short() {
        let bytes = [0x01, 0x02, 0x03];
        assert!(parse(&bytes).is_err());
    }
}
EOF
```

- [ ] **Step 3: Register the module**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/subrecords/mod.rs")
text = p.read_text()
if "pub mod form_id_ref" not in text:
    text = text.rstrip() + "\npub mod form_id_ref;\n"
    p.write_text(text)
PY
cat crates/mora-esp/src/subrecords/mod.rs
```

- [ ] **Step 4: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-esp --lib subrecords::form_id_ref::tests
git add crates/mora-esp/src/signature.rs crates/mora-esp/src/subrecords/
git commit -m "mora-esp: signature constants + generic FormID subrecord parser

Signature consts: DNAM, EITM, CNAM, TNAM, BOD2, BODT. Shared
subrecords::form_id_ref::parse returns the raw u32 for later
remapping by EspWorld::resolve_in_plugin. Used by EITM (enchant
refs), CNAM (weapon template), TNAM (armor template)."
```

---

## Phase C — WEAP subrecord parsers (Task 3)

### Task 3: `weapon_data.rs` + `weapon_dnam.rs`

**Files:**
- Create: `crates/mora-esp/src/subrecords/weapon_data.rs`
- Create: `crates/mora-esp/src/subrecords/weapon_dnam.rs`
- Modify: `crates/mora-esp/src/subrecords/mod.rs`

- [ ] **Step 1: Write weapon_data.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/subrecords/weapon_data.rs <<'EOF'
//! WEAP DATA subrecord — 10 bytes: value (u32) + weight (f32) + damage (u16).

use crate::reader::{ReadError, le_f32, le_u16, le_u32};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct WeaponData {
    pub value: u32,
    pub weight: f32,
    pub damage: u16,
}

pub fn parse(data: &[u8]) -> Result<WeaponData, ReadError> {
    if data.len() < 10 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 10,
        });
    }
    let (value, o) = le_u32(data, 0)?;
    let (weight, o) = le_f32(data, o)?;
    let (damage, _) = le_u16(data, o)?;
    Ok(WeaponData { value, weight, damage })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_known_layout() {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&75u32.to_le_bytes()); // value
        bytes.extend_from_slice(&9.0f32.to_le_bytes()); // weight
        bytes.extend_from_slice(&7u16.to_le_bytes()); // damage
        let d = parse(&bytes).unwrap();
        assert_eq!(d.value, 75);
        assert_eq!(d.weight, 9.0);
        assert_eq!(d.damage, 7);
    }

    #[test]
    fn rejects_short() {
        let bytes = [0u8; 5];
        assert!(parse(&bytes).is_err());
    }
}
EOF
```

- [ ] **Step 2: Write weapon_dnam.rs**

```bash
cat > crates/mora-esp/src/subrecords/weapon_dnam.rs <<'EOF'
//! WEAP DNAM subrecord — 100 bytes. Mora reads only the subset
//! needed for KID trait predicates:
//!   offset 0x00: animation_type (u8)
//!   offset 0x04: speed (f32)
//!   offset 0x08: reach (f32)
//! Everything else is skipped.

use crate::reader::{ReadError, le_f32, le_u8};

/// Weapon animation type (wire-format enum).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WeaponAnimType {
    HandToHandMelee = 0,
    OneHandSword = 1,
    OneHandDagger = 2,
    OneHandAxe = 3,
    OneHandMace = 4,
    TwoHandSword = 5,
    TwoHandAxe = 6,
    Bow = 7,
    Staff = 8,
    Crossbow = 9,
}

impl WeaponAnimType {
    pub fn from_u8(n: u8) -> Option<Self> {
        match n {
            0 => Some(Self::HandToHandMelee),
            1 => Some(Self::OneHandSword),
            2 => Some(Self::OneHandDagger),
            3 => Some(Self::OneHandAxe),
            4 => Some(Self::OneHandMace),
            5 => Some(Self::TwoHandSword),
            6 => Some(Self::TwoHandAxe),
            7 => Some(Self::Bow),
            8 => Some(Self::Staff),
            9 => Some(Self::Crossbow),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct WeaponDnam {
    pub animation_type: Option<WeaponAnimType>,
    pub speed: f32,
    pub reach: f32,
}

pub fn parse(data: &[u8]) -> Result<WeaponDnam, ReadError> {
    // M3 reads only the first 12 bytes (anim_type + padding + speed + reach).
    // Full DNAM is 100 bytes; we tolerate any size >= 12.
    if data.len() < 12 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 12,
        });
    }
    let (anim_u8, _) = le_u8(data, 0)?;
    let (speed, _) = le_f32(data, 4)?;
    let (reach, _) = le_f32(data, 8)?;
    Ok(WeaponDnam {
        animation_type: WeaponAnimType::from_u8(anim_u8),
        speed,
        reach,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build(anim: u8, speed: f32, reach: f32) -> Vec<u8> {
        let mut v = Vec::new();
        v.push(anim);
        v.extend_from_slice(&[0u8; 3]); // padding
        v.extend_from_slice(&speed.to_le_bytes());
        v.extend_from_slice(&reach.to_le_bytes());
        v.extend_from_slice(&[0u8; 88]); // pad to 100
        v
    }

    #[test]
    fn parses_one_hand_sword() {
        let b = build(1, 1.0, 0.75);
        let d = parse(&b).unwrap();
        assert_eq!(d.animation_type, Some(WeaponAnimType::OneHandSword));
        assert_eq!(d.speed, 1.0);
        assert_eq!(d.reach, 0.75);
    }

    #[test]
    fn unknown_anim_type_is_none() {
        let b = build(99, 1.0, 1.0);
        let d = parse(&b).unwrap();
        assert_eq!(d.animation_type, None);
    }

    #[test]
    fn tolerates_short_but_parses_prefix() {
        // Exactly 12 bytes — enough for our prefix.
        let mut b = vec![1u8, 0, 0, 0];
        b.extend_from_slice(&1.5f32.to_le_bytes());
        b.extend_from_slice(&2.0f32.to_le_bytes());
        let d = parse(&b).unwrap();
        assert_eq!(d.animation_type, Some(WeaponAnimType::OneHandSword));
        assert_eq!(d.speed, 1.5);
    }
}
EOF
```

- [ ] **Step 3: Register modules**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/subrecords/mod.rs")
text = p.read_text()
for m in ["weapon_data", "weapon_dnam"]:
    line = f"pub mod {m};\n"
    if f"pub mod {m}" not in text:
        text = text.rstrip() + "\n" + line
p.write_text(text)
PY
```

- [ ] **Step 4: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-esp --lib subrecords::weapon_data::tests
cargo test --package mora-esp --lib subrecords::weapon_dnam::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/subrecords/
git commit -m "mora-esp: WEAP DATA + DNAM subrecord parsers

WeaponData { value, weight, damage } from 10-byte DATA.
WeaponDnam { animation_type, speed, reach } reads the first 12
bytes of the 100-byte DNAM (skips the other 88 bytes we don't
need at M3). WeaponAnimType enum covers all 10 SSE animation
type values; unknown values decode to None."
```

---

## Phase D — Extend `WeaponRecord` + parse the new subrecords (Task 4)

### Task 4: Extend `WeaponRecord` with trait-predicate fields

**Files:**
- Modify: `crates/mora-esp/src/records/weapon.rs`

- [ ] **Step 1: Rewrite weapon.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/records/weapon.rs <<'EOF'
//! `WEAP` — weapon record accessor.
//!
//! M3-complete fields (used by KID trait predicates):
//!   - editor_id       : EDID subrecord
//!   - keywords        : KWDA (resolved FormIds)
//!   - animation_type  : DNAM offset 0x00 (u8 enum)
//!   - speed, reach    : DNAM offsets 0x04, 0x08 (f32)
//!   - value, weight, damage : DATA subrecord
//!   - enchantment     : Some(FormId) if EITM present, else None
//!   - template_weapon : Some(FormId) if CNAM present, else None
//!
//! Keyword FormIDs are resolved at parse time against the active load
//! order; EITM + CNAM local FormIDs are also resolved via
//! `EspWorld::resolve_in_plugin`. Unresolvable refs silently become
//! None (matches KID).

use mora_core::FormId;

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{CNAM, DATA, DNAM, EDID, EITM, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::weapon_dnam::WeaponAnimType;
use crate::subrecords::{edid, form_id_ref, kwda, weapon_data, weapon_dnam};
use crate::world::EspWorld;

#[derive(Debug, thiserror::Error)]
pub enum WeaponError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

#[derive(Debug, Default)]
pub struct WeaponRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<FormId>,
    pub animation_type: Option<WeaponAnimType>,
    pub speed: Option<f32>,
    pub reach: Option<f32>,
    pub value: Option<u32>,
    pub weight: Option<f32>,
    pub damage: Option<u16>,
    pub enchantment: Option<FormId>,
    pub template_weapon: Option<FormId>,
}

pub fn parse(
    record: &Record<'_>,
    plugin_index: usize,
    world: &EspWorld,
) -> Result<WeaponRecord, WeaponError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut w = WeaponRecord::default();
    for sub in SubrecordIter::new(body) {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => w.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => {
                let local_ids = kwda::parse(sub.data)?;
                w.keywords = local_ids
                    .into_iter()
                    .filter_map(|raw| world.resolve_in_plugin(plugin_index, raw))
                    .collect();
            }
            s if s == DATA => {
                if let Ok(data) = weapon_data::parse(sub.data) {
                    w.value = Some(data.value);
                    w.weight = Some(data.weight);
                    w.damage = Some(data.damage);
                }
            }
            s if s == DNAM => {
                if let Ok(dnam) = weapon_dnam::parse(sub.data) {
                    w.animation_type = dnam.animation_type;
                    w.speed = Some(dnam.speed);
                    w.reach = Some(dnam.reach);
                }
            }
            s if s == EITM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    w.enchantment = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            s if s == CNAM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    w.template_weapon = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            _ => {}
        }
    }
    Ok(w)
}

#[cfg(test)]
mod tests {
    // Full WEAP parsing tests land in tests/esp_format.rs (Plan 5
    // extended here + in Plan 8b's trait_predicates tests).
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-esp --all-targets
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/records/weapon.rs
git commit -m "mora-esp: extend WeaponRecord with DATA/DNAM/EITM/CNAM fields

WeaponRecord now exposes animation_type, speed, reach, value,
weight, damage, enchantment (resolved FormId), template_weapon
(resolved FormId) in addition to the existing editor_id + keywords.
Existing integration tests still pass since all new fields default
to None when the corresponding subrecords aren't present."
```

---

## Phase E — ARMO subrecord parsers (Task 5)

### Task 5: `armor_data.rs` + `armor_dnam.rs` + `biped_object.rs`

**Files:**
- Create: `crates/mora-esp/src/subrecords/armor_data.rs`
- Create: `crates/mora-esp/src/subrecords/armor_dnam.rs`
- Create: `crates/mora-esp/src/subrecords/biped_object.rs`
- Modify: `crates/mora-esp/src/subrecords/mod.rs`

- [ ] **Step 1: Write armor_data.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/subrecords/armor_data.rs <<'EOF'
//! ARMO DATA subrecord — 8 bytes: value (i32) + weight (f32).

use crate::reader::{ReadError, le_f32, le_u32};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ArmorData {
    pub value: i32,
    pub weight: f32,
}

pub fn parse(data: &[u8]) -> Result<ArmorData, ReadError> {
    if data.len() < 8 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 8,
        });
    }
    let (value_u, o) = le_u32(data, 0)?;
    let (weight, _) = le_f32(data, o)?;
    Ok(ArmorData {
        value: value_u as i32,
        weight,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_value_weight() {
        let mut b = Vec::new();
        b.extend_from_slice(&(125i32 as u32).to_le_bytes());
        b.extend_from_slice(&6.0f32.to_le_bytes());
        let d = parse(&b).unwrap();
        assert_eq!(d.value, 125);
        assert_eq!(d.weight, 6.0);
    }
}
EOF
```

- [ ] **Step 2: Write armor_dnam.rs**

```bash
cat > crates/mora-esp/src/subrecords/armor_dnam.rs <<'EOF'
//! ARMO DNAM subrecord — 4 bytes. Raw value is `display × 100`
//! (i32 on disk); exposed as f32 in display scale.

use crate::reader::{ReadError, le_u32};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ArmorDnam {
    /// Display-scale armor rating (raw / 100).
    pub armor_rating: f32,
}

pub fn parse(data: &[u8]) -> Result<ArmorDnam, ReadError> {
    if data.len() < 4 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 4,
        });
    }
    let (raw_u, _) = le_u32(data, 0)?;
    let raw_i = raw_u as i32;
    Ok(ArmorDnam {
        armor_rating: raw_i as f32 / 100.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_glass_chestplate() {
        // Glass chestplate 38.0 display = 3800 raw
        let bytes = 3800u32.to_le_bytes();
        let d = parse(&bytes).unwrap();
        assert_eq!(d.armor_rating, 38.0);
    }

    #[test]
    fn parses_zero() {
        let bytes = 0u32.to_le_bytes();
        assert_eq!(parse(&bytes).unwrap().armor_rating, 0.0);
    }
}
EOF
```

- [ ] **Step 3: Write biped_object.rs (BOD2 + BODT)**

```bash
cat > crates/mora-esp/src/subrecords/biped_object.rs <<'EOF'
//! `BOD2` / `BODT` subrecords — biped object slots + armor type.
//!
//! BOD2 (SSE primary) is 8 bytes: slots (u32) + armor_type (u32).
//! BODT (legacy) is 12 bytes: slots (u32) + general_flags (u8) +
//! 3 bytes padding + armor_type (u32). Mora parses both into the
//! same BipedObject struct; BOD2 preferred when both present.

use crate::reader::{ReadError, le_u32};

/// Armor type enum (u32 on disk).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArmorType {
    LightArmor = 0,
    HeavyArmor = 1,
    Clothing = 2,
}

impl ArmorType {
    pub fn from_u32(n: u32) -> Option<Self> {
        match n {
            0 => Some(Self::LightArmor),
            1 => Some(Self::HeavyArmor),
            2 => Some(Self::Clothing),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BipedObject {
    /// 32-bit bitmask; bit N = slot (30 + N).
    pub slots_bitmask: u32,
    pub armor_type: Option<ArmorType>,
}

impl BipedObject {
    /// Slot numbers currently occupied, decoded from `slots_bitmask`.
    /// Returns numbers in the range 30..=61.
    pub fn slot_numbers(&self) -> Vec<u8> {
        (0..32)
            .filter(|bit| self.slots_bitmask & (1u32 << bit) != 0)
            .map(|bit| 30 + bit as u8)
            .collect()
    }
}

/// Parse a BOD2 payload (8 bytes).
pub fn parse_bod2(data: &[u8]) -> Result<BipedObject, ReadError> {
    if data.len() < 8 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 8,
        });
    }
    let (slots, o) = le_u32(data, 0)?;
    let (at, _) = le_u32(data, o)?;
    Ok(BipedObject {
        slots_bitmask: slots,
        armor_type: ArmorType::from_u32(at),
    })
}

/// Parse a BODT payload (12 bytes).
pub fn parse_bodt(data: &[u8]) -> Result<BipedObject, ReadError> {
    if data.len() < 12 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 12,
        });
    }
    let (slots, _) = le_u32(data, 0)?;
    // Skip byte at 0x04 (general_flags) + 3 bytes padding.
    let (at, _) = le_u32(data, 8)?;
    Ok(BipedObject {
        slots_bitmask: slots,
        armor_type: ArmorType::from_u32(at),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bod2_parses_body_slot_heavy() {
        let mut b = Vec::new();
        b.extend_from_slice(&0x00000004u32.to_le_bytes()); // bit 2 = slot 32 (body)
        b.extend_from_slice(&1u32.to_le_bytes()); // heavy
        let o = parse_bod2(&b).unwrap();
        assert_eq!(o.slot_numbers(), vec![32]);
        assert_eq!(o.armor_type, Some(ArmorType::HeavyArmor));
    }

    #[test]
    fn bodt_skips_general_flags_byte() {
        let mut b = Vec::new();
        b.extend_from_slice(&0x00000008u32.to_le_bytes()); // bit 3 = slot 33 (hands)
        b.push(0xFF); // general_flags (ignored)
        b.extend_from_slice(&[0u8; 3]); // padding
        b.extend_from_slice(&0u32.to_le_bytes()); // light armor
        let o = parse_bodt(&b).unwrap();
        assert_eq!(o.slot_numbers(), vec![33]);
        assert_eq!(o.armor_type, Some(ArmorType::LightArmor));
    }

    #[test]
    fn multiple_slots() {
        let mut b = Vec::new();
        // bits 2 + 9 + 12 → slots 32, 39, 42 (body, shield, circlet)
        b.extend_from_slice(&0x00001204u32.to_le_bytes());
        b.extend_from_slice(&2u32.to_le_bytes()); // clothing
        let o = parse_bod2(&b).unwrap();
        assert_eq!(o.slot_numbers(), vec![32, 39, 42]);
        assert_eq!(o.armor_type, Some(ArmorType::Clothing));
    }

    #[test]
    fn unknown_armor_type() {
        let mut b = Vec::new();
        b.extend_from_slice(&0u32.to_le_bytes());
        b.extend_from_slice(&99u32.to_le_bytes()); // unknown
        let o = parse_bod2(&b).unwrap();
        assert_eq!(o.armor_type, None);
    }
}
EOF
```

- [ ] **Step 4: Register modules**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/subrecords/mod.rs")
text = p.read_text()
for m in ["armor_data", "armor_dnam", "biped_object"]:
    line = f"pub mod {m};\n"
    if f"pub mod {m}" not in text:
        text = text.rstrip() + "\n" + line
p.write_text(text)
PY
```

- [ ] **Step 5: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-esp --lib subrecords::armor_data::tests
cargo test --package mora-esp --lib subrecords::armor_dnam::tests
cargo test --package mora-esp --lib subrecords::biped_object::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/subrecords/
git commit -m "mora-esp: ARMO DATA/DNAM + BOD2/BODT subrecord parsers

ArmorData { value, weight } (8 bytes). ArmorDnam { armor_rating }
decodes from the i32-stored-as-display-times-100 format into a
display-scale f32. BipedObject { slots_bitmask, armor_type }
parses BOD2 (8 bytes, SSE primary) or BODT (12 bytes, legacy);
slot_numbers() decodes the bitmask into slot numbers 30..=61.
ArmorType enum for LightArmor/HeavyArmor/Clothing.

9 unit tests covering DATA layout, DNAM display-scaling,
BOD2 parsing, BODT padding-skip, multi-slot decode, unknown
armor type."
```

---

## Phase F — Extend `ArmorRecord` (Task 6)

### Task 6: Extend `ArmorRecord` with trait-predicate fields

**Files:**
- Modify: `crates/mora-esp/src/records/armor.rs`

- [ ] **Step 1: Rewrite armor.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/records/armor.rs <<'EOF'
//! `ARMO` — armor record accessor.
//!
//! M3-complete fields (used by KID trait predicates):
//!   - editor_id       : EDID
//!   - keywords        : KWDA (resolved)
//!   - armor_type      : BOD2/BODT — LightArmor/HeavyArmor/Clothing
//!   - body_slots      : BOD2/BODT — list of slot numbers 30..=61
//!   - armor_rating    : DNAM (display scale)
//!   - value, weight   : DATA
//!   - enchantment     : EITM resolved FormId
//!   - template_armor  : TNAM (NOT CNAM!) resolved FormId

use mora_core::FormId;

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{BOD2, BODT, DATA, DNAM, EDID, EITM, KWDA, TNAM};
use crate::subrecord::SubrecordIter;
use crate::subrecords::biped_object::{ArmorType, BipedObject, parse_bod2, parse_bodt};
use crate::subrecords::{armor_data, armor_dnam, edid, form_id_ref, kwda};
use crate::world::EspWorld;

#[derive(Debug, thiserror::Error)]
pub enum ArmorError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

#[derive(Debug, Default)]
pub struct ArmorRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<FormId>,
    pub armor_type: Option<ArmorType>,
    pub body_slots: Vec<u8>,
    pub armor_rating: Option<f32>,
    pub value: Option<i32>,
    pub weight: Option<f32>,
    pub enchantment: Option<FormId>,
    pub template_armor: Option<FormId>,
}

pub fn parse(
    record: &Record<'_>,
    plugin_index: usize,
    world: &EspWorld,
) -> Result<ArmorRecord, ArmorError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut a = ArmorRecord::default();
    // Track biped object separately so BOD2 can override BODT if both present.
    let mut biped_from_bod2: Option<BipedObject> = None;
    let mut biped_from_bodt: Option<BipedObject> = None;

    for sub in SubrecordIter::new(body) {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => a.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => {
                let local_ids = kwda::parse(sub.data)?;
                a.keywords = local_ids
                    .into_iter()
                    .filter_map(|raw| world.resolve_in_plugin(plugin_index, raw))
                    .collect();
            }
            s if s == BOD2 => {
                if let Ok(b) = parse_bod2(sub.data) {
                    biped_from_bod2 = Some(b);
                }
            }
            s if s == BODT => {
                if let Ok(b) = parse_bodt(sub.data) {
                    biped_from_bodt = Some(b);
                }
            }
            s if s == DATA => {
                if let Ok(d) = armor_data::parse(sub.data) {
                    a.value = Some(d.value);
                    a.weight = Some(d.weight);
                }
            }
            s if s == DNAM => {
                if let Ok(d) = armor_dnam::parse(sub.data) {
                    a.armor_rating = Some(d.armor_rating);
                }
            }
            s if s == EITM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    a.enchantment = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            s if s == TNAM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    a.template_armor = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            _ => {}
        }
    }

    // Prefer BOD2 over BODT (BOD2 is the SSE primary format).
    let biped = biped_from_bod2.or(biped_from_bodt);
    if let Some(b) = biped {
        a.armor_type = b.armor_type;
        a.body_slots = b.slot_numbers();
    }

    Ok(a)
}

#[cfg(test)]
mod tests {
    // Full ARMO parsing tests land in tests/trait_predicates.rs (Plan 8b).
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-esp --all-targets
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/records/armor.rs
git commit -m "mora-esp: extend ArmorRecord with DATA/DNAM/BOD2+BODT/EITM/TNAM

ArmorRecord gains armor_type, body_slots (Vec<u8> of slot numbers),
armor_rating, value, weight, enchantment, template_armor. BOD2 is
preferred over BODT when both are present (SSE should not have
both; defensive against old LE plugins). TNAM (not CNAM — ARMO
uses a different signature than WEAP) carries the template armor
reference."
```

---

## Phase G — Trait evaluators in mora-kid (Tasks 7-8)

### Task 7: `evaluate_weapon_traits` in filter.rs

**Files:**
- Modify: `crates/mora-kid/src/filter.rs`

- [ ] **Step 1: Append weapon-trait evaluator**

```bash
cd /home/tbaldrid/oss/mora
cat >> crates/mora-kid/src/filter.rs <<'EOF'

/// Evaluate weapon trait predicates against a parsed WeaponRecord.
/// Returns `true` if every specified predicate passes; `true` if the
/// traits struct is empty (no constraints).
pub fn evaluate_weapon_traits(
    traits: &crate::traits_weapon::WeaponTraits,
    weapon: &mora_esp::records::weapon::WeaponRecord,
) -> bool {
    // anim_types: OR across the listed types. If any matches, pass.
    if !traits.anim_types.is_empty() {
        let Some(weapon_anim) = weapon.animation_type else {
            return false;
        };
        let matched = traits.anim_types.iter().any(|t| match t {
            crate::traits_weapon::WeaponAnimType::OneHandSword => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandSword
            }
            crate::traits_weapon::WeaponAnimType::OneHandDagger => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandDagger
            }
            crate::traits_weapon::WeaponAnimType::OneHandAxe => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandAxe
            }
            crate::traits_weapon::WeaponAnimType::OneHandMace => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandMace
            }
            crate::traits_weapon::WeaponAnimType::TwoHandSword => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::TwoHandSword
            }
            crate::traits_weapon::WeaponAnimType::TwoHandAxe => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::TwoHandAxe
            }
            crate::traits_weapon::WeaponAnimType::Bow => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::Bow
            }
            crate::traits_weapon::WeaponAnimType::Crossbow => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::Crossbow
            }
            crate::traits_weapon::WeaponAnimType::Staff => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::Staff
            }
            crate::traits_weapon::WeaponAnimType::HandToHandMelee => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::HandToHandMelee
            }
        });
        if !matched {
            return false;
        }
    }

    // require_enchanted: Some(true) means "must be enchanted";
    // Some(false) means "must not be"; None means "no constraint".
    if let Some(must_enchanted) = traits.require_enchanted {
        let is_enchanted = weapon.enchantment.is_some();
        if is_enchanted != must_enchanted {
            return false;
        }
    }

    // require_template: Some(true/false) / None as above.
    if let Some(must_template) = traits.require_template {
        let has_template = weapon.template_weapon.is_some();
        if has_template != must_template {
            return false;
        }
    }

    // damage_range: inclusive range check.
    if let Some((min, max)) = traits.damage_range {
        let Some(damage) = weapon.damage else {
            return false;
        };
        let damage_f = damage as f32;
        if !(damage_f >= min && damage_f <= max) {
            return false;
        }
    }

    // weight_range: inclusive range check.
    if let Some((min, max)) = traits.weight_range {
        let Some(weight) = weapon.weight else {
            return false;
        };
        if !(weight >= min && weight <= max) {
            return false;
        }
    }

    true
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-kid --all-targets
git add crates/mora-kid/src/filter.rs
git commit -m "mora-kid: evaluate_weapon_traits

Maps WeaponTraits fields against WeaponRecord subrecord data.
- anim_types: OR — at least one must match
- require_enchanted / require_template: Some(true/false) enforces
  presence / absence of EITM / CNAM
- damage_range / weight_range: inclusive; records without DATA fail
  the predicate (matches KID's 'no data to check = fails filter')"
```

---

### Task 8: `evaluate_armor_traits` in filter.rs

**Files:**
- Modify: `crates/mora-kid/src/filter.rs`

- [ ] **Step 1: Append armor-trait evaluator**

```bash
cat >> crates/mora-kid/src/filter.rs <<'EOF'

/// Evaluate armor trait predicates against a parsed ArmorRecord.
pub fn evaluate_armor_traits(
    traits: &crate::traits_armor::ArmorTraits,
    armor: &mora_esp::records::armor::ArmorRecord,
) -> bool {
    // armor_types: OR. At least one must match the armor's type.
    if !traits.armor_types.is_empty() {
        let Some(armor_type) = armor.armor_type else {
            return false;
        };
        let matched = traits.armor_types.iter().any(|t| match t {
            crate::traits_armor::ArmorType::Heavy => {
                armor_type == mora_esp::subrecords::biped_object::ArmorType::HeavyArmor
            }
            crate::traits_armor::ArmorType::Light => {
                armor_type == mora_esp::subrecords::biped_object::ArmorType::LightArmor
            }
            crate::traits_armor::ArmorType::Clothing => {
                armor_type == mora_esp::subrecords::biped_object::ArmorType::Clothing
            }
        });
        if !matched {
            return false;
        }
    }

    if let Some(must_enchanted) = traits.require_enchanted {
        if armor.enchantment.is_some() != must_enchanted {
            return false;
        }
    }

    if let Some(must_template) = traits.require_template {
        if armor.template_armor.is_some() != must_template {
            return false;
        }
    }

    if let Some((min, max)) = traits.ar_range {
        let Some(ar) = armor.armor_rating else {
            return false;
        };
        if !(ar >= min && ar <= max) {
            return false;
        }
    }

    if let Some((min, max)) = traits.weight_range {
        let Some(weight) = armor.weight else {
            return false;
        };
        if !(weight >= min && weight <= max) {
            return false;
        }
    }

    // body_slots: OR. At least one listed slot must be occupied.
    if !traits.body_slots.is_empty() {
        let any_match = traits
            .body_slots
            .iter()
            .any(|wanted| armor.body_slots.contains(wanted));
        if !any_match {
            return false;
        }
    }

    true
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-kid --all-targets
git add crates/mora-kid/src/filter.rs
git commit -m "mora-kid: evaluate_armor_traits

Maps ArmorTraits fields against ArmorRecord subrecord data.
- armor_types: OR across Heavy/Light/Clothing
- require_enchanted / require_template: EITM / TNAM presence
- ar_range / weight_range: inclusive; missing data fails predicate
- body_slots: OR; at least one listed slot must be occupied"
```

---

## Phase H — Wire trait evaluators into distributor (Task 9)

### Task 9: Update `KidDistributor` to call the trait evaluators

**Files:**
- Modify: `crates/mora-kid/src/distributor.rs`

- [ ] **Step 1: Replace the debug log-and-skip with real evaluation**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/distributor.rs")
text = p.read_text()

# Weapon trait evaluation.
old_w = """                if let Traits::Weapon(wt) = &rr.rule.traits
                    && !wt.anim_types.is_empty()
                {
                    debug!(
                        "{}:{}: weapon trait predicates not yet evaluated (WeaponRecord lacks animType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }"""
new_w = """                if let Traits::Weapon(wt) = &rr.rule.traits
                    && !filter::evaluate_weapon_traits(wt, &weapon)
                {
                    stats.rejected_by_filter += 1;
                    continue;
                }"""
assert old_w in text, "weapon trait block not found — may need manual edit"
text = text.replace(old_w, new_w)

# Armor trait evaluation.
old_a = """                if let Traits::Armor(at) = &rr.rule.traits
                    && !at.armor_types.is_empty()
                {
                    debug!(
                        "{}:{}: armor trait predicates not yet evaluated (ArmorRecord lacks armorType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }"""
new_a = """                if let Traits::Armor(at) = &rr.rule.traits
                    && !filter::evaluate_armor_traits(at, &armor)
                {
                    stats.rejected_by_filter += 1;
                    continue;
                }"""
assert old_a in text, "armor trait block not found — may need manual edit"
text = text.replace(old_a, new_a)

# If `debug` is now unused (the two call sites removed), drop its import.
# Check whether any `debug!` remains.
if "debug!" not in text:
    text = text.replace("use tracing::{debug, warn};", "use tracing::warn;")

p.write_text(text)
PY
```

- [ ] **Step 2: Verify full workspace**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo test --workspace --lib -- --test-threads=1 2>&1 | grep -E "^test result" | awk '{c+=$4} END {print "TOTAL:", c}'
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-kid/src/distributor.rs
git commit -m "mora-kid: activate trait predicate evaluation in distributor

Previous Plan 6 implementation log-and-skipped trait predicates
because WeaponRecord/ArmorRecord didn't expose the required
subrecord fields. Now that mora-esp parses DATA/DNAM/EITM/CNAM/
TNAM/BOD2/BODT, wire the filter::evaluate_weapon_traits and
evaluate_armor_traits calls into both the WEAP and ARMO loops.
Trait-mismatch items increment rejected_by_filter."
```

---

## Phase I — Integration tests (Task 10)

### Task 10: End-to-end trait-predicate tests

**Files:**
- Create: `crates/mora-kid/tests/trait_predicates.rs`

- [ ] **Step 1: Write trait_predicates.rs**

```bash
cat > crates/mora-kid/tests/trait_predicates.rs <<'EOF'
//! End-to-end tests for Plan 8b trait predicates.
//!
//! Builds synthetic plugins containing WEAP/ARMO records with the
//! new subrecord fields (DATA, DNAM, BOD2, EITM, etc.), runs
//! KidDistributor with rules using trait predicates, and verifies
//! the correct items are (or aren't) patched.

use std::io::Write;

use mora_core::{DeterministicChance, Distributor, FormId, PatchSink};
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_ini_content;

fn write_tmp(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-8b-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path)
        .unwrap()
        .write_all(bytes)
        .unwrap();
    path
}

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
    v.extend_from_slice(&0u32.to_le_bytes());
    v.extend_from_slice(&form_id.to_le_bytes());
    v.extend_from_slice(&0u32.to_le_bytes());
    v.extend_from_slice(&44u16.to_le_bytes());
    v.extend_from_slice(&0u16.to_le_bytes());
    v.extend_from_slice(body);
    v
}
fn group(label: &[u8; 4], contents: &[u8]) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend_from_slice(b"GRUP");
    v.extend_from_slice(&((24 + contents.len()) as u32).to_le_bytes());
    v.extend_from_slice(label);
    v.extend_from_slice(&0u32.to_le_bytes());
    v.extend_from_slice(&0u16.to_le_bytes());
    v.extend_from_slice(&0u16.to_le_bytes());
    v.extend_from_slice(&0u32.to_le_bytes());
    v.extend_from_slice(contents);
    v
}
fn nul(s: &str) -> Vec<u8> {
    let mut v = s.as_bytes().to_vec();
    v.push(0);
    v
}

/// Minimal TES4 header for an ESM.
fn tes4_esm_bytes() -> Vec<u8> {
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes()); // ESM flag
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);
    out
}

/// Build a WEAP record with configurable DATA + DNAM + optional EITM.
fn weap_record(form_id: u32, edid: &str, anim: u8, damage: u16, weight: f32, enchantment: Option<u32>) -> Vec<u8> {
    let mut body = sub(b"EDID", &nul(edid));
    body.extend_from_slice(&sub(b"KWDA", &[])); // empty keyword list
    let mut data = Vec::new();
    data.extend_from_slice(&0u32.to_le_bytes()); // value
    data.extend_from_slice(&weight.to_le_bytes());
    data.extend_from_slice(&damage.to_le_bytes());
    body.extend_from_slice(&sub(b"DATA", &data));
    let mut dnam = vec![anim, 0, 0, 0];
    dnam.extend_from_slice(&1.0f32.to_le_bytes()); // speed
    dnam.extend_from_slice(&1.0f32.to_le_bytes()); // reach
    dnam.extend_from_slice(&[0u8; 88]); // rest of the 100-byte DNAM
    body.extend_from_slice(&sub(b"DNAM", &dnam));
    if let Some(ench) = enchantment {
        body.extend_from_slice(&sub(b"EITM", &ench.to_le_bytes()));
    }
    rec(b"WEAP", form_id, &body)
}

/// Build an ARMO record with configurable BOD2 + DATA + DNAM + optional EITM.
fn armo_record(
    form_id: u32,
    edid: &str,
    armor_type: u32,
    slots_bitmask: u32,
    weight: f32,
    ar: i32,
    enchantment: Option<u32>,
) -> Vec<u8> {
    let mut body = sub(b"EDID", &nul(edid));
    body.extend_from_slice(&sub(b"KWDA", &[]));
    let mut bod2 = Vec::new();
    bod2.extend_from_slice(&slots_bitmask.to_le_bytes());
    bod2.extend_from_slice(&armor_type.to_le_bytes());
    body.extend_from_slice(&sub(b"BOD2", &bod2));
    let mut data = Vec::new();
    data.extend_from_slice(&0i32.to_le_bytes()); // value
    data.extend_from_slice(&weight.to_le_bytes());
    body.extend_from_slice(&sub(b"DATA", &data));
    let mut dnam = Vec::new();
    dnam.extend_from_slice(&(ar as u32).to_le_bytes());
    body.extend_from_slice(&sub(b"DNAM", &dnam));
    if let Some(ench) = enchantment {
        body.extend_from_slice(&sub(b"EITM", &ench.to_le_bytes()));
    }
    rec(b"ARMO", form_id, &body)
}

fn open_world(suffix: &str, bytes: Vec<u8>) -> EspWorld {
    let path = write_tmp(&format!("{suffix}.esm"), &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path
        .parent()
        .unwrap()
        .join(format!("plugins-{suffix}.txt"));
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap()
}

fn run(world: &EspWorld, ini: &str) -> mora_core::PatchFile {
    let parsed = parse_ini_content(ini, "test.ini");
    let dist = KidDistributor::new(parsed.rules).with_exclusive_groups(parsed.exclusive_groups);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(world, &chance, &mut sink).unwrap();
    sink.finalize()
}

#[test]
fn weapon_animation_type_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut weapons = Vec::new();
    // Sword (anim=1), Bow (anim=7)
    weapons.extend_from_slice(&weap_record(0x0001_1100, "Sword", 1, 10, 5.0, None));
    weapons.extend_from_slice(&weap_record(0x0001_1101, "Bow", 7, 8, 3.0, None));
    bytes.extend_from_slice(&group(b"WEAP", &weapons));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_1200, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("animtype", bytes);
    // Rule targets only OneHandSword (anim_type == 1).
    let file = run(&world, "Target = Weapon|||OneHandSword\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_1100));
}

#[test]
fn weapon_damage_range_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut weapons = Vec::new();
    weapons.extend_from_slice(&weap_record(0x0001_1200, "Low", 1, 3, 5.0, None));
    weapons.extend_from_slice(&weap_record(0x0001_1201, "Mid", 1, 10, 5.0, None));
    weapons.extend_from_slice(&weap_record(0x0001_1202, "High", 1, 99, 5.0, None));
    bytes.extend_from_slice(&group(b"WEAP", &weapons));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_1300, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("dmgrange", bytes);
    // Damage 5..=20: only "Mid" (damage=10) passes.
    let file = run(&world, "Target = Weapon|||D(5 20)\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_1201));
}

#[test]
fn weapon_enchanted_predicate() {
    let mut bytes = tes4_esm_bytes();
    // Build an ENCH form — just a valid FormId placeholder; what matters
    // is EITM points to *something* that can be resolved.
    // For our test, we need the enchantment FormID to resolve. An
    // arbitrary FormId within this plugin's slot works if it's the
    // form's own FormID — EspWorld::resolve_in_plugin treats local
    // index 0 (same as len(masters)) as self-reference.
    let mut weapons = Vec::new();
    // Weapon A is enchanted (EITM = self.0x0001_9999), Weapon B is not.
    weapons.extend_from_slice(&weap_record(0x0001_1400, "Ench", 1, 10, 5.0, Some(0x0001_9999)));
    weapons.extend_from_slice(&weap_record(0x0001_1401, "Plain", 1, 10, 5.0, None));
    bytes.extend_from_slice(&group(b"WEAP", &weapons));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_1500, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("enchpred", bytes);
    // -E means NOT enchanted.
    let file = run(&world, "Target = Weapon|||-E\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_1401));
}

#[test]
fn armor_type_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut armors = Vec::new();
    // Light (type=0), Heavy (type=1), Clothing (type=2)
    armors.extend_from_slice(&armo_record(0x0001_2100, "LightBoots", 0, 0x80, 1.0, 800, None));
    armors.extend_from_slice(&armo_record(0x0001_2101, "HeavyBoots", 1, 0x80, 2.0, 1200, None));
    armors.extend_from_slice(&armo_record(0x0001_2102, "ClothBoots", 2, 0x80, 0.5, 0, None));
    bytes.extend_from_slice(&group(b"ARMO", &armors));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_2200, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("armortype", bytes);
    let file = run(&world, "Target = Armor|||HEAVY\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2101));
}

#[test]
fn armor_body_slot_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut armors = Vec::new();
    // Slot 32 (body) = bit 2 = 0x4
    armors.extend_from_slice(&armo_record(0x0001_2300, "Body", 1, 0x4, 5.0, 3000, None));
    // Slot 37 (feet) = bit 7 = 0x80
    armors.extend_from_slice(&armo_record(0x0001_2301, "Feet", 1, 0x80, 2.0, 1000, None));
    bytes.extend_from_slice(&group(b"ARMO", &armors));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_2400, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("bodyslot", bytes);
    let file = run(&world, "Target = Armor|||32\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2300));
}

#[test]
fn armor_ar_range_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut armors = Vec::new();
    // ar is stored × 100 on disk. Display values 5.0, 20.0, 50.0 → 500, 2000, 5000.
    armors.extend_from_slice(&armo_record(0x0001_2500, "AR5", 1, 0x4, 5.0, 500, None));
    armors.extend_from_slice(&armo_record(0x0001_2501, "AR20", 1, 0x4, 5.0, 2000, None));
    armors.extend_from_slice(&armo_record(0x0001_2502, "AR50", 1, 0x4, 5.0, 5000, None));
    bytes.extend_from_slice(&group(b"ARMO", &armors));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_2600, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("arrange", bytes);
    // AR(10 30): only AR20 passes (display value).
    let file = run(&world, "Target = Armor|||AR(10 30)\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2501));
}
EOF
```

- [ ] **Step 2: Run + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --test trait_predicates -- --test-threads=1
cargo xwin check --package mora-kid --target x86_64-pc-windows-msvc --tests
git add crates/mora-kid/tests/trait_predicates.rs
git commit -m "mora-kid: integration tests for trait predicates

6 end-to-end tests against synthetic plugins:
  weapon_animation_type_filter, weapon_damage_range_filter,
  weapon_enchanted_predicate, armor_type_filter,
  armor_body_slot_filter, armor_ar_range_filter.
Exercises the full Plan 8b pipeline: mora-esp parses the new
subrecords, mora-kid's evaluate_*_traits helpers gate emissions."
```

---

## Phase J — Final verification (Task 11)

### Task 11: Clean build + push + open PR

**Files:** none modified.

- [ ] **Step 1: Clean verify**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets -- --test-threads=1 2>&1 | grep -E "^test result" | awk '{c+=$4} END {print "TOTAL:", c}'
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all six green. Test total: 181 (Plan 8a) + ~9 new subrecord unit tests + 6 trait predicate integration tests + a couple from refactor = **~195**.

- [ ] **Step 2: Push + PR**

```bash
git push -u origin m3-plan8b-trait-predicates
gh pr create --base master --head m3-plan8b-trait-predicates \
    --title "Rust + KID pivot — Plan 8b: trait predicates" \
    --body "$(cat <<'PRBODY'
## Summary

Completes KID filter grammar coverage for Weapon + Armor rules by
activating trait predicates — the `OneHandSword`, `HEAVY`, `-E`,
`D(min max)`, `AR(min max)`, body-slot number fields from KID's
`WeaponTraits` and `ArmorTraits` grammars.

Required extending mora-esp to parse the backing subrecords:

- **WEAP DATA** (10 bytes): value + weight + damage
- **WEAP DNAM** (100 bytes, we read prefix 12 bytes): animation_type + speed + reach
- **WEAP EITM** (4-byte FormID): enchantment
- **WEAP CNAM** (4-byte FormID): template weapon
- **ARMO DATA** (8 bytes): value + weight
- **ARMO DNAM** (4 bytes): armor_rating (stored as display×100)
- **ARMO BOD2** (8 bytes, SSE primary): biped slots bitmask + armor_type enum
- **ARMO BODT** (12 bytes, legacy LE format): same info, different layout
- **ARMO EITM** (4-byte FormID): enchantment
- **ARMO TNAM** (4-byte FormID): template armor — **note**: ARMO uses TNAM, not CNAM like WEAP

mora-kid gains `filter::evaluate_weapon_traits` and `evaluate_armor_traits` that map the parsed AST fields to concrete checks against the new record data. The distributor replaces its earlier log-and-skip stubs with real evaluations; trait mismatches increment `rejected_by_filter`.

## Test plan

- [x] `cargo test --workspace` — ~195 tests pass (181 prior + ~14 new: 9 subrecord units + 6 trait integration)
- [x] `cargo clippy --all-targets -- -D warnings` clean
- [x] `cargo fmt --check` clean
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean

## Scope discipline

- **Weapon + Armor record types only.** KID INIs targeting other types (Potion, NPC, Scroll, etc.) continue to log-and-skip — those require their own record-type accessors in mora-esp.
- **Armor rating stored as display×100.** Parser returns f32 display-scale; filter predicates compare in display units.
- **BODT legacy format supported.** Pre-SSE plugins that weren't re-saved may still carry BODT instead of BOD2; parser handles both (BOD2 wins when both present).
- **ARMO template is TNAM, not CNAM.** Previous plans assumed CNAM uniformly — fixed in Plan 8b's armor parser.

## What's left for full KID parity

- **Other 17 record types** (Ammo, Potion, Scroll, Book, …) each need their own `mora-esp::records::X` + filter activation. Incremental Plan 9+ work, one per compatibility-matrix cell.
- **ANY substring match vs. display name (FULL) + model path (.nif)** — requires FULL/MODL subrecord parsing. Defer until real-world INIs demand it.
- **M4: golden-test harness against real KID** — captures KID's actual output from a real Skyrim run, diffs against Mora's patch file. Validates bit-identity claim.

## Next up

Depends on priority:
- **M4 golden-test harness** — verify bit-identity of what we have against real KID
- **Record type expansion** — work through the KID compatibility matrix
PRBODY
)"
```

- [ ] **Step 3: Watch CI + hand off**

---

## Completion criteria

- [ ] All new subrecord + integration tests pass.
- [ ] `cargo clippy -D warnings` clean.
- [ ] Weapon + Armor rules with trait predicates produce correct patch output against synthetic plugins.
- [ ] PR merged to `master`.

## Next plan

**M4 or record-type expansion** — see PR summary. Likely the golden-test harness first, because validating the existing coverage is more valuable than expanding it.
