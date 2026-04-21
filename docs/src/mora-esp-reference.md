# mora-esp Reference

Source of truth for the binary formats + load-order conventions
`mora-esp` handles. **Target: Skyrim Special Edition 1.5.97+
(including AE 1.6.x).** Oldrim (1.9.32) has a different record
version and zlib compression instead of LZ4 — not supported.

All multi-byte integers are little-endian.

## ESP file structure

A `.esp` / `.esl` / `.esm` file is a flat stream:

```
[TES4 file-header record]
[Group: WEAP] [Group: ARMO] [Group: NPC_] ...
```

The `TES4` record is always first. Following it are top-level groups,
one per record type. Some groups (CELL, WRLD) contain nested groups.

### Record header (24 bytes)

| Offset | Size | Field               | Notes                                      |
|--------|------|---------------------|--------------------------------------------|
| 0      | 4    | signature           | 4-byte ASCII (e.g. `WEAP`)                 |
| 4      | 4    | data_size           | Size of record body (after header)         |
| 8      | 4    | flags               | Record flags (see below)                   |
| 12     | 4    | form_id             | 32-bit FormID (high byte = local mod idx)  |
| 16     | 4    | version_control_info | Ignored by Mora                            |
| 20     | 2    | record_version      | `44` on SSE                                |
| 22     | 2    | unknown             | Ignored                                    |

**Key record flags (low 32 bits):**

| Bit        | Meaning                         |
|------------|---------------------------------|
| 0x00000001 | ESM master flag (TES4 only)     |
| 0x00000200 | Light Master (ESL) — TES4 only  |
| 0x00000400 | Deleted                         |
| 0x00040000 | Compressed (LZ4, SSE/AE)        |

### Group header (24 bytes)

| Offset | Size | Field      | Notes                                              |
|--------|------|------------|----------------------------------------------------|
| 0      | 4    | magic      | Always `GRUP`                                      |
| 4      | 4    | group_size | Size of header + all contained records/groups      |
| 8      | 4    | label      | Type-specific (top-level = 4-byte signature)       |
| 12     | 4    | group_type | 0=top-level, 1-10=nested variants                  |
| 16     | 2    | timestamp  | Ignored                                            |
| 18     | 2    | vc_info    | Ignored                                            |
| 20     | 4    | unknown    | Ignored                                            |

For M2 Mora walks only top-level groups (group_type = 0) and reads
the nested records directly without recursing into nested groups.

### Subrecord header (6 bytes)

| Offset | Size | Field     | Notes                        |
|--------|------|-----------|------------------------------|
| 0      | 4    | signature | 4-byte ASCII (e.g. `EDID`)   |
| 4      | 2    | data_size | Subrecord payload size       |

**XXXX override:** if data_size would overflow `u16`, a preceding
`XXXX` subrecord (4-byte signature + 2-byte fixed "04" + 4-byte u32
real_size) supplies the real size. The next subrecord reads
`real_size` bytes. Rare for Weapon/Armor; Mora handles it defensively.

### Compressed records (SSE / AE)

If `flags & 0x00040000`, the record body is:

```
[4-byte LE u32 decompressed_size][LZ4-compressed data]
```

The compressed stream is **LZ4 Frame** format (`lz4_flex::frame`).
The `decompressed_size` prefix is Bethesda's addition — the LZ4
Frame header that follows has its own metadata, but the prefix is
authoritative for output-buffer sizing.

**Verification step:** Task 8 fetches the xEdit / CommonLibSSE-NG
reference implementation to confirm Frame-vs-Block and prefix
handling before writing production code.

### TES4 file header

The `TES4` record appears first. Its subrecords:

- **`HEDR`** (12 bytes) — version (f32, e.g. 1.7), num_records (i32), next_object_id (u32).
- **`CNAM`** — author string (NUL-terminated).
- **`SNAM`** — description string (NUL-terminated).
- **`MAST`** — master plugin filename (NUL-terminated). Repeats for each master. The **order is the local-master-index ordering**: the first MAST in the file is local index 0, the second is local index 1, etc.
- **`DATA`** — placeholder after each MAST (8 bytes, ignored).
- **`ONAM`** — override form IDs (ignored for M2).
- **`INTV`** — internal version (ignored).
- **`INCC`** — increment count (ignored).

**ESL flag detection:** the TES4 record's *own* flags field (bit
`0x00000200`) is set for ESL-flagged plugins.

## plugins.txt

Standard SSE/AE `plugins.txt` lives at
`%USERPROFILE%\AppData\Local\Skyrim Special Edition\plugins.txt` (or
equivalent under Proton: `compatdata/<app>/pfx/drive_c/users/steamuser/AppData/Local/...`).

MO2 stores its own profile-specific plugins.txt under the profile's
`plugins.txt` path.

**Format:**
- UTF-8, typically CRLF line endings (Mora accepts LF too).
- Lines starting with `#` are comments.
- Blank lines are ignored.
- Lines starting with `*` are **active** plugins (loaded into the game).
- Lines without `*` are **inactive** (present in Data/ but not loaded).
- Plugin filenames are case-insensitive on Windows (matched against
  actual Data/ directory via case-insensitive lookup).

Mora parses this file to enumerate active plugins in load order. The
first `*`-prefixed line is the "next after base game" — Bethesda's
own ESMs (Skyrim.esm, Update.esm, Dawnguard.esm, HearthFires.esm,
Dragonborn.esm) are **implicitly** loaded first in that order and are
not listed in `plugins.txt`.

## Load order resolution

**Mod index layout (SSE/AE):**

| Mod index | Plugin                                           |
|-----------|--------------------------------------------------|
| 0x00      | Skyrim.esm                                       |
| 0x01      | Update.esm                                       |
| 0x02      | Dawnguard.esm (if installed)                     |
| 0x03      | HearthFires.esm (if installed)                   |
| 0x04      | Dragonborn.esm (if installed)                    |
| 0x05-0xFD | User ESMs + ESPs in plugins.txt order            |
| 0xFE      | **Light-slot pool** — all ESL-flagged plugins    |
| 0xFF      | Reserved for newly-created forms at runtime      |

**Light-slot sub-indexing:** each ESL-flagged plugin gets a 12-bit
slot within the 0xFE pool. The first ESL in load order is slot 0x000,
the next 0x001, up to 0xFFF. A form in that plugin has FormID
`0xFE XYZ <local_3_hex_digits>` where XYZ is the slot.

**Mora's remapping rule:**

For a form referenced via a plugin P with local mod index L:
1. Look up P's master list: masters[L] = referenced plugin M.
2. If M is ESL-flagged: FormID = `(0xFE << 24) | (slot(M) << 12) | (local_form_id & 0xFFF)`.
3. Else: FormID = `(global_mod_index(M) << 24) | (local_form_id & 0xFFFFFF)`.

Special case: local mod index L where L == len(masters) is the
plugin **itself** (references to its own forms).

## Synthetic-fixture test strategy

`mora-esp` tests build ESP byte buffers inline in Rust code,
exercising header parsing + subrecord iteration without depending on
external files. A helper module in `tests/fixtures.rs` provides
reusable builders:

- `TES4Builder` — emits a TES4 record with configurable flags, masters, HEDR.
- `RecordBuilder` — emits an arbitrary record with signature, form_id, subrecords.
- `SubrecordBuilder` — emits `signature + le_u16 size + bytes`.
- `GroupBuilder` — emits a top-level group wrapping records.

Tests compose these into full plugin byte buffers, write to tmp
files, mmap-load, and assert.

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

