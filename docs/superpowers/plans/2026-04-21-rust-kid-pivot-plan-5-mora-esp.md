# Rust + KID Pivot — Plan 5: `mora-esp` (M2, Part 2 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver `mora-esp` end-to-end — mmap-backed ESP/ESL/ESM reader, plugins.txt parser + load-order resolver, master-index remapping, and enough subrecord parsing + record-type accessors that `mora-kid` (Plan 6) can iterate weapons and armors with their keywords. Replace the `EspWorld` placeholder in `mora-core::distributor` with the real indexed view.

**Architecture:** `mora-esp` is a compiler-side crate — no SKSE, no game memory, no `unsafe` beyond what `memmap2` requires. The crate reads from disk via mmap, parses ESP binary format (TES4 / Record / Group / Subrecord headers), handles LZ4-compressed records on the SSE flag bit, resolves plugin load order + master indices, and exposes record iteration via typed accessors. Tests use synthetic ESP fixtures built inline as byte buffers — same pattern as `address_library.rs`.

**Tech Stack:** Rust 1.90. New workspace deps: `lz4_flex` (pure-Rust LZ4, cross-compile-friendly). Existing deps already pulled by `mora-esp`: `mora-core`, `thiserror`, `memmap2`, `rayon`, `tracing`.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope discipline:**
- **Target: Skyrim Special Edition** (1.5.97+, including AE 1.6.x). Oldrim (1.9.32) is NOT supported — different compression, different record version.
- **Subrecord parsers in Plan 5:** EDID (editor ID), KWDA (keyword list), trait-bearing subrecords only for Weapon + Armor (DNAM). More record types land in later plans as `mora-kid` coverage grows.
- **No mora-kid code in this plan.** Plan 6+ writes the KID INI parser + distributor.
- **No `unsafe`** except the minimum needed to return slices into an mmap's lifetime.
- **Localized strings are out of scope.** Weapon/Armor accessors return raw data, not the FULL name (which lives in the `.STRINGS` file for localized ESMs). EDID is not localized; it's the only string we need for M2+M3.

---

## File Structure

**Modified:**
- `Cargo.toml` (workspace root) — add `lz4_flex = "0.11"` to `[workspace.dependencies]`
- `crates/mora-esp/Cargo.toml` — depend on `lz4_flex`
- `crates/mora-esp/src/lib.rs` — replace stub with module tree + re-exports
- `crates/mora-core/src/distributor.rs` — replace `EspWorld` placeholder with a re-export of the real type from `mora-esp`
- `crates/mora-core/Cargo.toml` — depend on `mora-esp` *(creates a forward dep; acceptable because `mora-esp` already depends on `mora-core`, but we need to break the cycle — see Task 15 for the approach)*

**Created:**
- `crates/mora-esp/src/reader.rs` — byte-reader helpers (LE u8/u16/u32, slices, cursor)
- `crates/mora-esp/src/signature.rs` — 4-char signature type + well-known constants
- `crates/mora-esp/src/tes4.rs` — TES4 file-header parser (magic, flags, masters)
- `crates/mora-esp/src/record.rs` — record-header parser + record iteration
- `crates/mora-esp/src/group.rs` — group-header parser + group iteration
- `crates/mora-esp/src/subrecord.rs` — subrecord header + iterator over a record's body
- `crates/mora-esp/src/compression.rs` — LZ4 decompression for the compressed-record flag
- `crates/mora-esp/src/plugins_txt.rs` — plugins.txt parser
- `crates/mora-esp/src/load_order.rs` — load-order resolver (full + light slots)
- `crates/mora-esp/src/plugin.rs` — `EspPlugin` — a single mmapped plugin with master list
- `crates/mora-esp/src/world.rs` — `EspWorld` — indexed view across all active plugins
- `crates/mora-esp/src/subrecords/mod.rs` — subrecord-parser module root
- `crates/mora-esp/src/subrecords/edid.rs` — editor-ID parser
- `crates/mora-esp/src/subrecords/kwda.rs` — keyword-array parser
- `crates/mora-esp/src/records/mod.rs` — typed-record-accessor module root
- `crates/mora-esp/src/records/weapon.rs` — `WeaponRecord` accessor
- `crates/mora-esp/src/records/armor.rs` — `ArmorRecord` accessor
- `crates/mora-esp/tests/esp_format.rs` — integration tests against synthetic ESP fixtures
- `crates/mora-esp/tests/fixtures.rs` — shared fixture-building helpers
- `docs/src/mora-esp-reference.md` — source-of-truth: ESP binary format (SSE), plugins.txt spec, load order rules

---

## Phase A — Reference documentation (Task 1)

### Task 1: Write `docs/src/mora-esp-reference.md`

**Files:**
- Create: `docs/src/mora-esp-reference.md`

Single source of truth for the ESP binary layout + plugins.txt spec + load-order resolution. Cited throughout the plan.

- [ ] **Step 1: Write the doc**

```bash
cd /home/tbaldrid/oss/mora
cat > docs/src/mora-esp-reference.md <<'EOF'
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

EOF
```

- [ ] **Step 2: Commit**

```bash
git add docs/src/mora-esp-reference.md
git commit -m "docs: mora-esp reference

ESP binary format (SSE/AE), plugins.txt spec, load-order rules
(including ESL light-slot pool + sub-indexing), master-index
remapping algorithm, LZ4 Frame compression notes, synthetic-fixture
test strategy. Cited by every task in Plan 5."
```

---

## Phase B — Crate scaffold + workspace dep (Task 2)

### Task 2: Add `lz4_flex` dep and restructure `mora-esp/src/lib.rs`

**Files:**
- Modify: `Cargo.toml` (workspace)
- Modify: `crates/mora-esp/Cargo.toml`
- Modify: `crates/mora-esp/src/lib.rs`
- Create stubs: all the `mora-esp/src/**/*.rs` modules listed in the File Structure section above

- [ ] **Step 1: Add lz4_flex to workspace deps**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("Cargo.toml")
text = p.read_text()
marker = "rayon = \"1\"\n"
new_line = "lz4_flex = \"0.11\"\n"
if "lz4_flex" not in text:
    text = text.replace(marker, marker + new_line, 1)
    p.write_text(text)
PY
grep lz4_flex Cargo.toml
```

Expected: `lz4_flex = "0.11"` appears.

- [ ] **Step 2: Add lz4_flex to mora-esp/Cargo.toml**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/Cargo.toml")
text = p.read_text()
marker = "memmap2.workspace = true\n"
new_line = "lz4_flex.workspace = true\n"
if "lz4_flex" not in text:
    text = text.replace(marker, marker + new_line, 1)
    p.write_text(text)
PY
grep lz4_flex crates/mora-esp/Cargo.toml
```

Expected: `lz4_flex.workspace = true` appears.

- [ ] **Step 3: Rewrite lib.rs with module tree**

```bash
cat > crates/mora-esp/src/lib.rs <<'EOF'
//! Memory-mapped ESP/ESL/ESM reader, `plugins.txt` parser, load-order
//! resolver, and indexed record view for Mora.
//!
//! Target: Skyrim Special Edition 1.5.97+ (LZ4-compressed records,
//! record version 44, full + light-slot mod indices).
//!
//! See `docs/src/mora-esp-reference.md` for the binary format + load
//! order rules.

pub mod compression;
pub mod group;
pub mod load_order;
pub mod plugin;
pub mod plugins_txt;
pub mod reader;
pub mod record;
pub mod records;
pub mod signature;
pub mod subrecord;
pub mod subrecords;
pub mod tes4;
pub mod world;

pub use plugin::EspPlugin;
pub use signature::Signature;
pub use world::EspWorld;
EOF
```

- [ ] **Step 4: Create stub modules**

```bash
for m in compression group load_order plugin plugins_txt reader record signature subrecord tes4 world; do
    cat > "crates/mora-esp/src/$m.rs" <<EOF
//! Stub. Populated in its own task of Plan 5.
EOF
done

mkdir -p crates/mora-esp/src/records crates/mora-esp/src/subrecords
for dir in records subrecords; do
    cat > "crates/mora-esp/src/$dir/mod.rs" <<EOF
//! Stub. Populated in later tasks of Plan 5.
EOF
done
```

Three specific stubs need placeholder types for re-exports in `lib.rs`:

```bash
cat > crates/mora-esp/src/plugin.rs <<'EOF'
//! Stub. Populated in Task 13.

/// Placeholder — real impl in Task 13.
pub struct EspPlugin;
EOF

cat > crates/mora-esp/src/world.rs <<'EOF'
//! Stub. Populated in Task 14.

/// Placeholder — real impl in Task 14.
pub struct EspWorld;
EOF

cat > crates/mora-esp/src/signature.rs <<'EOF'
//! Stub. Populated in Task 3.

/// Placeholder — real impl in Task 3.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Signature(pub [u8; 4]);
EOF
```

- [ ] **Step 5: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add Cargo.toml crates/mora-esp/
git commit -m "mora-esp: crate scaffold with module tree + lz4_flex dep

lz4_flex = 0.11 added to workspace deps (pure-Rust LZ4 for
SSE/AE compressed records). mora-esp src/: module tree for reader,
signature, tes4, record, group, subrecord, compression, plugins_txt,
load_order, plugin, world, records/, subrecords/. All stubs minimal."
```

---

## Phase C — Byte reader + Signature type (Tasks 3-4)

### Task 3: Implement `signature.rs` — 4-char signature type

**Files:**
- Modify: `crates/mora-esp/src/signature.rs`

- [ ] **Step 1: Write signature.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/signature.rs <<'EOF'
//! 4-byte ASCII signature type + well-known constants.

/// A 4-byte ASCII signature used for records, groups, and subrecords.
///
/// Examples: `TES4`, `WEAP`, `ARMO`, `EDID`, `KWDA`, `GRUP`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Signature(pub [u8; 4]);

impl Signature {
    pub const fn new(bytes: &[u8; 4]) -> Self {
        Signature([bytes[0], bytes[1], bytes[2], bytes[3]])
    }

    pub const fn as_bytes(self) -> [u8; 4] {
        self.0
    }

    /// View as an ASCII string. Signatures are always valid ASCII in
    /// well-formed plugins.
    pub fn as_str(&self) -> &str {
        // SAFETY: signatures are ASCII by definition. Malformed input
        // gets checked at parse time; this display accessor trusts the
        // signature was validated upstream.
        core::str::from_utf8(&self.0).unwrap_or("????")
    }
}

impl std::fmt::Display for Signature {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

// Well-known signatures.

pub const TES4: Signature = Signature::new(b"TES4");
pub const GRUP: Signature = Signature::new(b"GRUP");

// Record types (subset — M2 scope)
pub const WEAP: Signature = Signature::new(b"WEAP");
pub const ARMO: Signature = Signature::new(b"ARMO");

// Subrecord types (subset)
pub const HEDR: Signature = Signature::new(b"HEDR");
pub const MAST: Signature = Signature::new(b"MAST");
pub const DATA: Signature = Signature::new(b"DATA");
pub const EDID: Signature = Signature::new(b"EDID");
pub const KWDA: Signature = Signature::new(b"KWDA");
pub const XXXX: Signature = Signature::new(b"XXXX");

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn well_known_signatures_display() {
        assert_eq!(TES4.to_string(), "TES4");
        assert_eq!(WEAP.to_string(), "WEAP");
        assert_eq!(EDID.to_string(), "EDID");
    }

    #[test]
    fn as_bytes_roundtrip() {
        let s = Signature::new(b"TEST");
        assert_eq!(s.as_bytes(), *b"TEST");
    }

    #[test]
    fn equality() {
        assert_eq!(Signature::new(b"WEAP"), WEAP);
        assert_ne!(WEAP, ARMO);
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-esp --lib signature::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/signature.rs
git commit -m "mora-esp: Signature type + well-known constants

4-byte ASCII signatures (TES4/GRUP/WEAP/ARMO/HEDR/MAST/EDID/KWDA/XXXX).
Display / as_str / as_bytes accessors. 3 unit tests."
```

---

### Task 4: Implement `reader.rs` — byte-reader helpers

**Files:**
- Modify: `crates/mora-esp/src/reader.rs`

- [ ] **Step 1: Write reader.rs**

```bash
cat > crates/mora-esp/src/reader.rs <<'EOF'
//! Byte-reader helpers for parsing ESP binary data.
//!
//! A `Reader` wraps a `&[u8]` and tracks a cursor; methods read
//! little-endian integers and arbitrary byte slices, advancing the
//! cursor.

use crate::signature::Signature;

/// Errors from byte-level reads.
#[derive(Debug, thiserror::Error)]
pub enum ReadError {
    #[error("unexpected end of input (needed {needed} bytes at offset {offset})")]
    Truncated { offset: usize, needed: usize },
}

/// Read a little-endian `u8` at offset. Returns `(value, new_offset)`.
pub fn le_u8(bytes: &[u8], offset: usize) -> Result<(u8, usize), ReadError> {
    if offset >= bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 1 });
    }
    Ok((bytes[offset], offset + 1))
}

pub fn le_u16(bytes: &[u8], offset: usize) -> Result<(u16, usize), ReadError> {
    if offset + 2 > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 2 });
    }
    Ok((
        u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap()),
        offset + 2,
    ))
}

pub fn le_u32(bytes: &[u8], offset: usize) -> Result<(u32, usize), ReadError> {
    if offset + 4 > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 4 });
    }
    Ok((
        u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap()),
        offset + 4,
    ))
}

pub fn le_f32(bytes: &[u8], offset: usize) -> Result<(f32, usize), ReadError> {
    let (u, o) = le_u32(bytes, offset)?;
    Ok((f32::from_bits(u), o))
}

/// Read a 4-byte signature.
pub fn read_signature(bytes: &[u8], offset: usize) -> Result<(Signature, usize), ReadError> {
    if offset + 4 > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: 4 });
    }
    let mut buf = [0u8; 4];
    buf.copy_from_slice(&bytes[offset..offset + 4]);
    Ok((Signature(buf), offset + 4))
}

/// Read an exact `n`-byte slice at offset. Returns `(slice, new_offset)`.
pub fn read_bytes<'a>(
    bytes: &'a [u8],
    offset: usize,
    n: usize,
) -> Result<(&'a [u8], usize), ReadError> {
    if offset + n > bytes.len() {
        return Err(ReadError::Truncated { offset, needed: n });
    }
    Ok((&bytes[offset..offset + n], offset + n))
}

/// Read a NUL-terminated string starting at `offset`, within at most
/// `max_len` bytes. Returns `(string_slice, bytes_consumed_including_nul)`.
/// If no NUL is found before `offset + max_len`, returns `Truncated`.
pub fn read_cstr(bytes: &[u8], offset: usize, max_len: usize) -> Result<(&str, usize), ReadError> {
    let (slice, _) = read_bytes(bytes, offset, max_len)?;
    let nul_pos = slice.iter().position(|&b| b == 0).ok_or(ReadError::Truncated {
        offset,
        needed: max_len + 1,
    })?;
    let s = core::str::from_utf8(&slice[..nul_pos]).map_err(|_| ReadError::Truncated {
        offset,
        needed: nul_pos,
    })?;
    Ok((s, nul_pos + 1))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn le_u32_roundtrip() {
        let bytes = 0x12345678u32.to_le_bytes();
        let (v, n) = le_u32(&bytes, 0).unwrap();
        assert_eq!(v, 0x12345678);
        assert_eq!(n, 4);
    }

    #[test]
    fn le_u16_happy() {
        let bytes = [0x34, 0x12, 0xFF];
        let (v, n) = le_u16(&bytes, 0).unwrap();
        assert_eq!(v, 0x1234);
        assert_eq!(n, 2);
    }

    #[test]
    fn truncation_error() {
        let bytes = [0x01, 0x02];
        match le_u32(&bytes, 0) {
            Err(ReadError::Truncated { needed: 4, offset: 0 }) => {}
            other => panic!("expected Truncated; got {other:?}"),
        }
    }

    #[test]
    fn read_signature_happy() {
        let bytes = b"TES4\0\0\0\0";
        let (sig, n) = read_signature(bytes, 0).unwrap();
        assert_eq!(sig, Signature::new(b"TES4"));
        assert_eq!(n, 4);
    }

    #[test]
    fn read_cstr_happy() {
        let bytes = b"Hello\0\0";
        let (s, consumed) = read_cstr(bytes, 0, 16).unwrap();
        assert_eq!(s, "Hello");
        assert_eq!(consumed, 6); // 5 chars + NUL
    }

    #[test]
    fn read_cstr_no_nul_is_truncated() {
        let bytes = b"noNulHere";
        match read_cstr(bytes, 0, 9) {
            Err(ReadError::Truncated { .. }) => {}
            other => panic!("expected Truncated; got {other:?}"),
        }
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib reader::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/reader.rs
git commit -m "mora-esp: byte-reader helpers

le_u8/u16/u32/f32 + read_signature + read_bytes + read_cstr against
a &[u8] cursor. ReadError::Truncated for short input. 6 unit tests
covering happy path, truncation, and NUL handling."
```

---

## Phase D — Header parsers (Tasks 5-7)

### Task 5: Implement `record.rs` — record header

**Files:**
- Modify: `crates/mora-esp/src/record.rs`

- [ ] **Step 1: Write record.rs**

```bash
cat > crates/mora-esp/src/record.rs <<'EOF'
//! Record header (24 bytes) parser.
//!
//! See `docs/src/mora-esp-reference.md`. Parses the header and
//! exposes the record body slice. Subrecord iteration is handled
//! in `subrecord.rs`; compression is `compression.rs`.

use crate::reader::{le_u16, le_u32, read_signature, ReadError};
use crate::signature::Signature;

pub const RECORD_HEADER_SIZE: usize = 24;

/// Bit mask for the "compressed" flag.
pub const RECORD_FLAG_COMPRESSED: u32 = 0x0004_0000;
/// Bit mask for the "deleted" flag.
pub const RECORD_FLAG_DELETED: u32 = 0x0000_0400;
/// Bit mask for the ESM master flag (TES4 only).
pub const RECORD_FLAG_MASTER: u32 = 0x0000_0001;
/// Bit mask for the "light master" (ESL) flag (TES4 only).
pub const RECORD_FLAG_LIGHT_MASTER: u32 = 0x0000_0200;

/// A parsed record header. Lifetime `'a` ties the body slice to the
/// source byte buffer (typically an mmap).
#[derive(Debug)]
pub struct Record<'a> {
    pub signature: Signature,
    pub flags: u32,
    pub form_id: u32,
    pub record_version: u16,
    /// The raw body bytes (does not include the 24-byte header).
    /// For compressed records this is still the compressed payload;
    /// decompression is applied separately.
    pub body: &'a [u8],
}

impl<'a> Record<'a> {
    /// `true` iff the compressed-record flag is set.
    pub fn is_compressed(&self) -> bool {
        self.flags & RECORD_FLAG_COMPRESSED != 0
    }

    /// `true` iff the deleted flag is set. Mora skips deleted records.
    pub fn is_deleted(&self) -> bool {
        self.flags & RECORD_FLAG_DELETED != 0
    }
}

/// Parse a record header + body from `bytes` starting at `offset`.
/// Returns `(record, new_offset)` — `new_offset` points to the byte
/// after the record body.
pub fn read_record<'a>(
    bytes: &'a [u8],
    offset: usize,
) -> Result<(Record<'a>, usize), ReadError> {
    let (signature, o) = read_signature(bytes, offset)?;
    let (data_size, o) = le_u32(bytes, o)?;
    let (flags, o) = le_u32(bytes, o)?;
    let (form_id, o) = le_u32(bytes, o)?;
    let (_vc_info, o) = le_u32(bytes, o)?;
    let (record_version, o) = le_u16(bytes, o)?;
    let (_unknown, o) = le_u16(bytes, o)?;

    let body_start = o;
    let body_end = body_start + data_size as usize;
    if body_end > bytes.len() {
        return Err(ReadError::Truncated {
            offset: body_start,
            needed: data_size as usize,
        });
    }
    let body = &bytes[body_start..body_end];

    Ok((
        Record {
            signature,
            flags,
            form_id,
            record_version,
            body,
        },
        body_end,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_record_header(
        sig: &[u8; 4],
        data_size: u32,
        flags: u32,
        form_id: u32,
        version: u16,
    ) -> Vec<u8> {
        let mut v = Vec::with_capacity(RECORD_HEADER_SIZE);
        v.extend_from_slice(sig);
        v.extend_from_slice(&data_size.to_le_bytes());
        v.extend_from_slice(&flags.to_le_bytes());
        v.extend_from_slice(&form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&version.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes()); // unknown
        v
    }

    #[test]
    fn parses_minimal_record_header() {
        let mut buf = make_record_header(b"WEAP", 0, 0, 0x0001_2EB7, 44);
        // zero-length body
        let (rec, next) = read_record(&buf, 0).unwrap();
        assert_eq!(rec.signature, Signature::new(b"WEAP"));
        assert_eq!(rec.form_id, 0x0001_2EB7);
        assert_eq!(rec.record_version, 44);
        assert_eq!(rec.body.len(), 0);
        assert_eq!(next, RECORD_HEADER_SIZE);
        buf.clear(); // silence unused
    }

    #[test]
    fn record_with_body() {
        let mut buf = make_record_header(b"WEAP", 6, 0, 0xDEAD_BEEF, 44);
        buf.extend_from_slice(b"abcdef");
        let (rec, next) = read_record(&buf, 0).unwrap();
        assert_eq!(rec.body, b"abcdef");
        assert_eq!(next, RECORD_HEADER_SIZE + 6);
    }

    #[test]
    fn compressed_flag_detected() {
        let buf = make_record_header(b"WEAP", 0, RECORD_FLAG_COMPRESSED, 0, 44);
        let (rec, _) = read_record(&buf, 0).unwrap();
        assert!(rec.is_compressed());
        assert!(!rec.is_deleted());
    }

    #[test]
    fn deleted_flag_detected() {
        let buf = make_record_header(b"WEAP", 0, RECORD_FLAG_DELETED, 0, 44);
        let (rec, _) = read_record(&buf, 0).unwrap();
        assert!(rec.is_deleted());
        assert!(!rec.is_compressed());
    }

    #[test]
    fn truncated_body_errors() {
        let mut buf = make_record_header(b"WEAP", 100, 0, 0, 44);
        buf.extend_from_slice(&[0u8; 10]); // only 10 body bytes, data_size says 100
        match read_record(&buf, 0) {
            Err(ReadError::Truncated { .. }) => {}
            other => panic!("expected Truncated; got {other:?}"),
        }
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib record::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/record.rs
git commit -m "mora-esp: record header parser

24-byte header parse → Record { signature, flags, form_id,
record_version, body: &[u8] }. is_compressed() / is_deleted() flag
accessors. 5 unit tests covering minimal header, body, compressed
flag, deleted flag, truncation."
```

---

### Task 6: Implement `group.rs` — group header + iteration

**Files:**
- Modify: `crates/mora-esp/src/group.rs`

- [ ] **Step 1: Write group.rs**

```bash
cat > crates/mora-esp/src/group.rs <<'EOF'
//! Group header (24 bytes) parser + top-level group iteration.

use crate::reader::{le_u16, le_u32, read_signature, ReadError};
use crate::signature::{Signature, GRUP};

pub const GROUP_HEADER_SIZE: usize = 24;

/// A parsed group header.
#[derive(Debug)]
pub struct Group<'a> {
    /// For top-level groups: the record-type signature (e.g. `WEAP`).
    /// For nested groups: a type-specific label (CELL parent id, etc.).
    pub label: [u8; 4],
    /// 0 = top-level; 1..=10 = nested variants.
    pub group_type: u32,
    /// Contents — the bytes after the 24-byte header, up to
    /// `group_size - 24` total.
    pub contents: &'a [u8],
}

impl<'a> Group<'a> {
    pub fn label_signature(&self) -> Signature {
        Signature(self.label)
    }

    pub fn is_top_level(&self) -> bool {
        self.group_type == 0
    }
}

/// Read a group header + contents. Returns `(group, new_offset)`.
/// `new_offset` points to the byte after the full group.
pub fn read_group<'a>(bytes: &'a [u8], offset: usize) -> Result<(Group<'a>, usize), ReadError> {
    let (magic, o) = read_signature(bytes, offset)?;
    if magic != GRUP {
        return Err(ReadError::Truncated {
            offset,
            needed: 4, // abused; magic mismatch is an error we report as truncation
        });
    }
    let (group_size, o) = le_u32(bytes, o)?;
    let mut label = [0u8; 4];
    label.copy_from_slice(&bytes[o..o + 4]);
    let o = o + 4;
    let (group_type, o) = le_u32(bytes, o)?;
    let (_timestamp, o) = le_u16(bytes, o)?;
    let (_vc_info, o) = le_u16(bytes, o)?;
    let (_unknown, o) = le_u32(bytes, o)?;

    let contents_start = o;
    let contents_end = offset + group_size as usize;
    if contents_end > bytes.len() || contents_end < contents_start {
        return Err(ReadError::Truncated {
            offset: contents_start,
            needed: group_size as usize - GROUP_HEADER_SIZE,
        });
    }
    let contents = &bytes[contents_start..contents_end];

    Ok((
        Group {
            label,
            group_type,
            contents,
        },
        contents_end,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_group_header(label: &[u8; 4], group_size: u32, group_type: u32) -> Vec<u8> {
        let mut v = Vec::with_capacity(GROUP_HEADER_SIZE);
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&group_size.to_le_bytes());
        v.extend_from_slice(label);
        v.extend_from_slice(&group_type.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v
    }

    #[test]
    fn parses_empty_top_level_weap_group() {
        // group_size = just the header = 24 bytes
        let buf = make_group_header(b"WEAP", GROUP_HEADER_SIZE as u32, 0);
        let (g, next) = read_group(&buf, 0).unwrap();
        assert_eq!(g.label_signature(), Signature::new(b"WEAP"));
        assert!(g.is_top_level());
        assert_eq!(g.contents.len(), 0);
        assert_eq!(next, GROUP_HEADER_SIZE);
    }

    #[test]
    fn parses_group_with_contents() {
        let payload = b"record bytes here";
        let group_size = (GROUP_HEADER_SIZE + payload.len()) as u32;
        let mut buf = make_group_header(b"WEAP", group_size, 0);
        buf.extend_from_slice(payload);
        let (g, next) = read_group(&buf, 0).unwrap();
        assert_eq!(g.contents, payload);
        assert_eq!(next, GROUP_HEADER_SIZE + payload.len());
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib group::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/group.rs
git commit -m "mora-esp: group header parser

24-byte group header → Group { label, group_type, contents: &[u8] }.
Top-level test (group_type == 0) + label_signature accessor. 2 unit
tests: empty WEAP group, group with payload."
```

---

### Task 7: Implement `subrecord.rs` — subrecord iteration

**Files:**
- Modify: `crates/mora-esp/src/subrecord.rs`

- [ ] **Step 1: Write subrecord.rs**

```bash
cat > crates/mora-esp/src/subrecord.rs <<'EOF'
//! Subrecord header (6 bytes) + iteration over a record body.
//!
//! Also handles the `XXXX` override for subrecords whose payload
//! size exceeds `u16::MAX`.

use crate::reader::{le_u16, le_u32, read_signature, ReadError};
use crate::signature::{Signature, XXXX};

pub const SUBRECORD_HEADER_SIZE: usize = 6;

#[derive(Debug)]
pub struct Subrecord<'a> {
    pub signature: Signature,
    pub data: &'a [u8],
}

/// Iterator over subrecords in a record body.
pub struct SubrecordIter<'a> {
    bytes: &'a [u8],
    offset: usize,
    /// Pending XXXX override — applied to the next subrecord.
    pending_size: Option<u32>,
}

impl<'a> SubrecordIter<'a> {
    pub fn new(body: &'a [u8]) -> Self {
        SubrecordIter {
            bytes: body,
            offset: 0,
            pending_size: None,
        }
    }
}

impl<'a> Iterator for SubrecordIter<'a> {
    type Item = Result<Subrecord<'a>, ReadError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.offset >= self.bytes.len() {
            return None;
        }
        let start = self.offset;
        let (sig, o) = match read_signature(self.bytes, start) {
            Ok(x) => x,
            Err(e) => return Some(Err(e)),
        };
        let (header_size, o) = match le_u16(self.bytes, o) {
            Ok(x) => x,
            Err(e) => return Some(Err(e)),
        };

        // XXXX override: the XXXX record declares the real size of the
        // NEXT subrecord. XXXX itself has u16 size = 4 and its payload
        // is a u32 real_size.
        if sig == XXXX {
            // Read the u32 real size from the 4-byte XXXX payload.
            if header_size != 4 {
                return Some(Err(ReadError::Truncated {
                    offset: o,
                    needed: 4,
                }));
            }
            let (real_size, next_o) = match le_u32(self.bytes, o) {
                Ok(x) => x,
                Err(e) => return Some(Err(e)),
            };
            self.pending_size = Some(real_size);
            self.offset = next_o;
            return self.next();
        }

        let data_size = self.pending_size.take().unwrap_or(header_size as u32);
        let data_start = o;
        let data_end = data_start + data_size as usize;
        if data_end > self.bytes.len() {
            return Some(Err(ReadError::Truncated {
                offset: data_start,
                needed: data_size as usize,
            }));
        }
        let data = &self.bytes[data_start..data_end];
        self.offset = data_end;

        Some(Ok(Subrecord {
            signature: sig,
            data,
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_subrecord(sig: &[u8; 4], payload: &[u8]) -> Vec<u8> {
        let mut v = Vec::with_capacity(SUBRECORD_HEADER_SIZE + payload.len());
        v.extend_from_slice(sig);
        v.extend_from_slice(&(payload.len() as u16).to_le_bytes());
        v.extend_from_slice(payload);
        v
    }

    #[test]
    fn iterates_two_subrecords() {
        let mut body = make_subrecord(b"EDID", b"IronSword\0");
        body.extend_from_slice(&make_subrecord(b"KWDA", &[0x01u8, 0x02, 0x03, 0x04]));
        let subs: Vec<_> = SubrecordIter::new(&body).collect::<Result<_, _>>().unwrap();
        assert_eq!(subs.len(), 2);
        assert_eq!(subs[0].signature.as_bytes(), *b"EDID");
        assert_eq!(subs[0].data, b"IronSword\0");
        assert_eq!(subs[1].signature.as_bytes(), *b"KWDA");
        assert_eq!(subs[1].data, &[0x01, 0x02, 0x03, 0x04]);
    }

    #[test]
    fn xxxx_override_applied_to_next_subrecord() {
        // XXXX says next subrecord's real size is 100,000 bytes.
        let mut body = Vec::new();
        body.extend_from_slice(b"XXXX");
        body.extend_from_slice(&4u16.to_le_bytes()); // header_size
        body.extend_from_slice(&100_000u32.to_le_bytes()); // real_size
        // Large EDID payload (100,000 bytes of 0xAB).
        body.extend_from_slice(b"EDID");
        body.extend_from_slice(&0u16.to_le_bytes()); // header_size (will be overridden)
        body.extend(std::iter::repeat(0xABu8).take(100_000));

        let subs: Vec<_> = SubrecordIter::new(&body).collect::<Result<_, _>>().unwrap();
        assert_eq!(subs.len(), 1);
        assert_eq!(subs[0].signature.as_bytes(), *b"EDID");
        assert_eq!(subs[0].data.len(), 100_000);
    }

    #[test]
    fn empty_body_yields_no_subrecords() {
        let body: Vec<u8> = Vec::new();
        let subs: Vec<_> = SubrecordIter::new(&body).collect::<Result<_, _>>().unwrap();
        assert!(subs.is_empty());
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib subrecord::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/subrecord.rs
git commit -m "mora-esp: subrecord iterator over a record body

SubrecordIter yields Subrecord { signature, data: &[u8] }. XXXX
override for payloads exceeding u16::MAX. 3 unit tests: two-subrecord
iteration, XXXX 100k payload, empty body."
```

---

## Phase E — LZ4 compression (Task 8)

### Task 8: Implement `compression.rs` — LZ4 decompression

**Files:**
- Modify: `crates/mora-esp/src/compression.rs`

**Step 0 (MANDATORY): Verify LZ4 format.** The plan claims SSE uses LZ4 Frame format with a 4-byte Bethesda-added decompressed-size prefix. This must be verified before implementation.

- [ ] **Step 0: Verify format against xEdit or CommonLibSSE-NG**

Fetch one of these sources and find the SSE compressed-record handling:
- `https://raw.githubusercontent.com/TES5Edit/TES5Edit/master/wbLZ4.pas` (xEdit's LZ4 wrapper)
- `https://raw.githubusercontent.com/CharmedBaryon/CommonLibSSE-NG/master/` — search for LZ4 in ESP/record handling

Confirm three things and note the findings in this task's commit message:
1. **Is it LZ4 Frame or LZ4 Block format?** (Frame has a magic header `0x184D2204`; Block is raw compressed bytes.)
2. **Is there a 4-byte decompressed-size prefix before the LZ4 data?**
3. **What `lz4_flex` API matches?** (`decompress` / `decompress_into` / `FrameDecoder::new(&[u8]).read_to_end(&mut buf)`)

If the plan's assumption is wrong, adjust the implementation below accordingly and note the correction in the commit.

- [ ] **Step 1: Write compression.rs**

Assuming Frame format + 4-byte prefix per plan:

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/compression.rs <<'EOF'
//! LZ4 decompression for SSE compressed records.
//!
//! Record body layout when `RECORD_FLAG_COMPRESSED` is set:
//!
//!   [4-byte LE u32 decompressed_size][LZ4 Frame data]
//!
//! Verified in Task 8 Step 0 against xEdit / CommonLibSSE-NG.

use std::io::Read;

use crate::reader::{le_u32, ReadError};

#[derive(Debug, thiserror::Error)]
pub enum DecompressError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("lz4 decompression: {0}")]
    Lz4(String),
    #[error("decompressed size mismatch: expected {expected}, got {actual}")]
    SizeMismatch { expected: usize, actual: usize },
}

/// Decompress a SSE-compressed record body.
///
/// `body` is the full record body (including the 4-byte prefix).
/// Returns the decompressed bytes as a `Vec<u8>` of length
/// `decompressed_size`.
pub fn decompress(body: &[u8]) -> Result<Vec<u8>, DecompressError> {
    let (decompressed_size, o) = le_u32(body, 0)?;
    let compressed = &body[o..];

    let mut out = Vec::with_capacity(decompressed_size as usize);
    let mut dec = lz4_flex::frame::FrameDecoder::new(compressed);
    dec.read_to_end(&mut out)
        .map_err(|e| DecompressError::Lz4(e.to_string()))?;

    if out.len() != decompressed_size as usize {
        return Err(DecompressError::SizeMismatch {
            expected: decompressed_size as usize,
            actual: out.len(),
        });
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn make_compressed(payload: &[u8]) -> Vec<u8> {
        let mut compressed = Vec::new();
        {
            let mut enc = lz4_flex::frame::FrameEncoder::new(&mut compressed);
            enc.write_all(payload).unwrap();
            enc.finish().unwrap();
        }
        let mut buf = Vec::new();
        buf.extend_from_slice(&(payload.len() as u32).to_le_bytes());
        buf.extend_from_slice(&compressed);
        buf
    }

    #[test]
    fn roundtrip_small_payload() {
        let payload = b"hello compressed world";
        let body = make_compressed(payload);
        let out = decompress(&body).unwrap();
        assert_eq!(out, payload);
    }

    #[test]
    fn roundtrip_larger_payload() {
        let payload: Vec<u8> = (0..10_000u32).flat_map(|i| i.to_le_bytes()).collect();
        let body = make_compressed(&payload);
        let out = decompress(&body).unwrap();
        assert_eq!(out, payload);
    }

    #[test]
    fn size_mismatch_detected() {
        // Lie about the decompressed size.
        let payload = b"hello";
        let mut compressed = Vec::new();
        {
            let mut enc = lz4_flex::frame::FrameEncoder::new(&mut compressed);
            enc.write_all(payload).unwrap();
            enc.finish().unwrap();
        }
        let mut body = Vec::new();
        body.extend_from_slice(&999u32.to_le_bytes()); // wrong size
        body.extend_from_slice(&compressed);
        match decompress(&body) {
            Err(DecompressError::SizeMismatch { expected: 999, actual: 5 }) => {}
            other => panic!("expected SizeMismatch; got {other:?}"),
        }
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib compression::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/compression.rs
git commit -m "mora-esp: LZ4 Frame decompression for SSE compressed records

decompress(body) reads a 4-byte LE u32 decompressed_size prefix
then inflates the LZ4 Frame payload via lz4_flex::frame::FrameDecoder.
Errors: Read (truncation), Lz4 (frame error), SizeMismatch (prefix
disagrees with actual inflated length). 3 round-trip tests: small
payload, 10k-element payload, size-mismatch detection.

<If Step 0 verification revealed the format is actually Block + 4-byte
prefix (no Frame header), update both code and commit message.>"
```

---

## Phase F — TES4 header (Task 9)

### Task 9: Implement `tes4.rs` — TES4 file-header parser

**Files:**
- Modify: `crates/mora-esp/src/tes4.rs`

- [ ] **Step 1: Write tes4.rs**

```bash
cat > crates/mora-esp/src/tes4.rs <<'EOF'
//! TES4 file-header parser.
//!
//! The TES4 record is always first in a plugin. It carries:
//!   - flags (ESM master bit, ESL light-master bit)
//!   - HEDR subrecord (version, num_records, next_object_id)
//!   - MAST subrecords (master plugin filenames, in local-index order)
//!   - CNAM (author), SNAM (description) — ignored by Mora

use crate::reader::{le_f32, le_u32, read_cstr, ReadError};
use crate::record::{read_record, RECORD_FLAG_LIGHT_MASTER, RECORD_FLAG_MASTER};
use crate::signature::{CNAM, HEDR, MAST, SNAM, TES4};
use crate::subrecord::SubrecordIter;

#[derive(Debug, thiserror::Error)]
pub enum Tes4Error {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("not a TES4 record: got signature {0}")]
    NotTes4(String),
    #[error("missing HEDR subrecord")]
    MissingHedr,
    #[error("bad HEDR size: expected 12, got {0}")]
    BadHedrSize(usize),
}

/// Parsed TES4 header.
#[derive(Debug)]
pub struct Tes4Header {
    /// Record flags — bit 0x01 = ESM, bit 0x200 = ESL.
    pub flags: u32,
    /// From HEDR: version (e.g. 1.7 for SSE).
    pub version: f32,
    /// From HEDR: number of records in the plugin.
    pub num_records: u32,
    /// From HEDR: next object ID for runtime-generated forms.
    pub next_object_id: u32,
    /// Master plugin filenames in local-index order.
    pub masters: Vec<String>,
    /// Author string (optional).
    pub author: Option<String>,
    /// Description string (optional).
    pub description: Option<String>,
}

impl Tes4Header {
    pub fn is_esm(&self) -> bool {
        self.flags & RECORD_FLAG_MASTER != 0
    }
    pub fn is_esl(&self) -> bool {
        self.flags & RECORD_FLAG_LIGHT_MASTER != 0
    }
}

/// Parse the TES4 header from the start of a plugin byte buffer.
pub fn parse_tes4(bytes: &[u8]) -> Result<Tes4Header, Tes4Error> {
    let (rec, _) = read_record(bytes, 0)?;
    if rec.signature != TES4 {
        return Err(Tes4Error::NotTes4(rec.signature.to_string()));
    }

    let mut version = 0.0f32;
    let mut num_records = 0u32;
    let mut next_object_id = 0u32;
    let mut masters = Vec::new();
    let mut author = None;
    let mut description = None;
    let mut hedr_seen = false;

    let mut iter = SubrecordIter::new(rec.body);
    while let Some(sub) = iter.next() {
        let sub = sub?;
        match sub.signature {
            s if s == HEDR => {
                if sub.data.len() != 12 {
                    return Err(Tes4Error::BadHedrSize(sub.data.len()));
                }
                let (v, o) = le_f32(sub.data, 0)?;
                let (n, o) = le_u32(sub.data, o)?;
                let (nxt, _) = le_u32(sub.data, o)?;
                version = v;
                num_records = n;
                next_object_id = nxt;
                hedr_seen = true;
            }
            s if s == MAST => {
                let (name, _) = read_cstr(sub.data, 0, sub.data.len())?;
                masters.push(name.to_string());
            }
            s if s == CNAM => {
                let (name, _) = read_cstr(sub.data, 0, sub.data.len())?;
                author = Some(name.to_string());
            }
            s if s == SNAM => {
                let (name, _) = read_cstr(sub.data, 0, sub.data.len())?;
                description = Some(name.to_string());
            }
            _ => {} // ONAM, INTV, INCC, DATA — ignored for M2
        }
    }

    if !hedr_seen {
        return Err(Tes4Error::MissingHedr);
    }

    Ok(Tes4Header {
        flags: rec.flags,
        version,
        num_records,
        next_object_id,
        masters,
        author,
        description,
    })
}

#[cfg(test)]
mod tests {
    // Tests use the shared fixtures module; see Task 21's
    // tests/fixtures.rs for TES4 builder helpers.
}
EOF
```

Task 9's in-module tests are minimal; richer testing lands via `tests/fixtures.rs` in Task 21.

Wait — the `signature.rs` module only declared TES4, GRUP, WEAP, ARMO, HEDR, MAST, DATA, EDID, KWDA, XXXX. We need CNAM and SNAM too. Add them:

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/signature.rs")
text = p.read_text()
marker = "pub const XXXX: Signature = Signature::new(b\"XXXX\");\n"
new_line = "pub const CNAM: Signature = Signature::new(b\"CNAM\");\npub const SNAM: Signature = Signature::new(b\"SNAM\");\n"
if "CNAM" not in text:
    text = text.replace(marker, marker + new_line)
    p.write_text(text)
PY
```

- [ ] **Step 2: Smoke-test the parser against a minimal fixture**

Write a quick inline test to confirm the parser wires up:

```bash
cat >> crates/mora-esp/src/tes4.rs <<'EOF'

#[cfg(test)]
mod smoke_tests {
    use super::*;
    use crate::record::RECORD_HEADER_SIZE;
    use crate::subrecord::SUBRECORD_HEADER_SIZE;

    fn build_minimal_tes4(masters: &[&str]) -> Vec<u8> {
        // Build subrecords
        let mut subs = Vec::new();
        // HEDR: version=1.7, num_records=0, next_object_id=0x800
        subs.extend_from_slice(b"HEDR");
        subs.extend_from_slice(&12u16.to_le_bytes());
        subs.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
        subs.extend_from_slice(&0u32.to_le_bytes());
        subs.extend_from_slice(&0x800u32.to_le_bytes());
        // Masters
        for m in masters {
            let name_bytes = m.as_bytes();
            let size = (name_bytes.len() + 1) as u16; // +1 for NUL
            subs.extend_from_slice(b"MAST");
            subs.extend_from_slice(&size.to_le_bytes());
            subs.extend_from_slice(name_bytes);
            subs.push(0); // NUL
            // DATA placeholder (8 bytes)
            subs.extend_from_slice(b"DATA");
            subs.extend_from_slice(&8u16.to_le_bytes());
            subs.extend_from_slice(&0u64.to_le_bytes());
        }

        // Build record header
        let mut buf = Vec::new();
        buf.extend_from_slice(b"TES4");
        buf.extend_from_slice(&(subs.len() as u32).to_le_bytes()); // data_size
        buf.extend_from_slice(&0x01u32.to_le_bytes()); // ESM flag
        buf.extend_from_slice(&0u32.to_le_bytes()); // form_id
        buf.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        buf.extend_from_slice(&44u16.to_le_bytes()); // version
        buf.extend_from_slice(&0u16.to_le_bytes()); // unknown
        buf.extend_from_slice(&subs);
        buf
    }

    #[test]
    fn parses_tes4_with_two_masters() {
        let buf = build_minimal_tes4(&["Skyrim.esm", "Update.esm"]);
        let h = parse_tes4(&buf).unwrap();
        assert!(h.is_esm());
        assert!(!h.is_esl());
        assert_eq!(h.version, 1.7);
        assert_eq!(h.masters, vec!["Skyrim.esm", "Update.esm"]);
    }

    #[test]
    fn parses_tes4_with_no_masters() {
        let buf = build_minimal_tes4(&[]);
        let h = parse_tes4(&buf).unwrap();
        assert!(h.masters.is_empty());
    }
}
EOF
```

- [ ] **Step 3: Verify + commit**

```bash
cargo test --package mora-esp --lib tes4
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/tes4.rs crates/mora-esp/src/signature.rs
git commit -m "mora-esp: TES4 file-header parser

parse_tes4(bytes) -> Tes4Header { flags, version, num_records,
next_object_id, masters, author, description }. is_esm() / is_esl()
flag accessors. CNAM/SNAM signatures added. 2 smoke tests: TES4
with two masters, TES4 with no masters."
```

---

## Phase G — plugins.txt + load order (Tasks 10-12)

### Task 10: Implement `plugins_txt.rs` — plugins.txt parser

**Files:**
- Modify: `crates/mora-esp/src/plugins_txt.rs`

- [ ] **Step 1: Write plugins_txt.rs**

```bash
cat > crates/mora-esp/src/plugins_txt.rs <<'EOF'
//! `plugins.txt` parser.
//!
//! Standard Bethesda plugins.txt format:
//!   - One plugin filename per line
//!   - Lines starting with `#` are comments
//!   - Blank lines ignored
//!   - Lines starting with `*` mark ACTIVE plugins
//!   - Filenames are case-insensitive (matched against Data/ directory)
//!   - CRLF or LF line endings

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginEntry {
    pub name: String,
    pub active: bool,
}

/// Parse a plugins.txt buffer into a list of entries, preserving
/// file order.
pub fn parse(content: &str) -> Vec<PluginEntry> {
    let mut entries = Vec::new();
    for line in content.lines() {
        let line = line.trim_end_matches('\r').trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (active, name) = if let Some(rest) = line.strip_prefix('*') {
            (true, rest.trim())
        } else {
            (false, line)
        };
        if name.is_empty() {
            continue;
        }
        entries.push(PluginEntry {
            name: name.to_string(),
            active,
        });
    }
    entries
}

/// Filter to only active entries.
pub fn active_plugins(entries: &[PluginEntry]) -> Vec<&PluginEntry> {
    entries.iter().filter(|e| e.active).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple() {
        let s = "# comment\n*Skyrim.esm\n*MyMod.esp\nInactive.esp\n";
        let e = parse(s);
        assert_eq!(e.len(), 3);
        assert_eq!(e[0].name, "Skyrim.esm");
        assert!(e[0].active);
        assert_eq!(e[1].name, "MyMod.esp");
        assert!(e[1].active);
        assert_eq!(e[2].name, "Inactive.esp");
        assert!(!e[2].active);
    }

    #[test]
    fn handles_crlf_line_endings() {
        let s = "*A.esm\r\n*B.esp\r\n";
        let e = parse(s);
        assert_eq!(e.len(), 2);
        assert_eq!(e[0].name, "A.esm");
        assert_eq!(e[1].name, "B.esp");
    }

    #[test]
    fn skips_blank_lines_and_comments() {
        let s = "\n# foo\n\n*A.esp\n  # bar\n";
        let e = parse(s);
        assert_eq!(e.len(), 1);
    }

    #[test]
    fn active_plugins_filter() {
        let entries = parse("*A.esp\nB.esp\n*C.esp\n");
        let active = active_plugins(&entries);
        assert_eq!(active.len(), 2);
        assert_eq!(active[0].name, "A.esp");
        assert_eq!(active[1].name, "C.esp");
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib plugins_txt::tests
git add crates/mora-esp/src/plugins_txt.rs
git commit -m "mora-esp: plugins.txt parser

parse(content) -> Vec<PluginEntry { name, active }>, preserving file
order. active_plugins filter. Handles CRLF/LF, comments, blank lines,
leading/trailing whitespace. 4 unit tests."
```

---

### Task 11: Implement `load_order.rs` — full + light slot resolution

**Files:**
- Modify: `crates/mora-esp/src/load_order.rs`

- [ ] **Step 1: Write load_order.rs**

```bash
cat > crates/mora-esp/src/load_order.rs <<'EOF'
//! Load-order resolver — maps plugin filenames to runtime mod indices.
//!
//! SSE/AE mod-index layout:
//!   0x00                 Skyrim.esm (implicit)
//!   0x01                 Update.esm (implicit)
//!   0x02-0x04            Dawnguard / HearthFires / Dragonborn (if present)
//!   0x05-0xFD            User ESMs + ESPs in plugins.txt order
//!   0xFE + 12-bit slot   ESL pool (all ESL-flagged plugins)
//!   0xFF                 Reserved (runtime forms)

use std::collections::HashMap;

/// One entry in the resolved load order.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LoadSlot {
    /// Full mod index (0x00..=0xFD).
    Full(u8),
    /// Light-slot pool — 0x000..=0xFFF within the 0xFE high-byte.
    Light(u16),
}

impl LoadSlot {
    /// The high byte of a FormID from this slot.
    pub fn high_byte(&self) -> u8 {
        match self {
            LoadSlot::Full(b) => *b,
            LoadSlot::Light(_) => 0xFE,
        }
    }

    /// Compose a full FormID from this slot + a local 24-bit id.
    /// For light slots, only 12 bits of local_id are used (0x000-0xFFF).
    pub fn compose_form_id(&self, local_id: u32) -> u32 {
        match self {
            LoadSlot::Full(b) => ((*b as u32) << 24) | (local_id & 0x00FF_FFFF),
            LoadSlot::Light(slot) => (0xFEu32 << 24) | ((*slot as u32) << 12) | (local_id & 0x0FFF),
        }
    }
}

/// Resolved load order — maps plugin filename (case-insensitive) to its slot.
#[derive(Debug, Default)]
pub struct LoadOrder {
    /// Preserves original casing of filenames.
    pub plugin_names: Vec<String>,
    /// Lowercase → slot.
    pub slots: HashMap<String, LoadSlot>,
}

impl LoadOrder {
    pub fn lookup(&self, plugin_name: &str) -> Option<&LoadSlot> {
        self.slots.get(&plugin_name.to_lowercase())
    }
}

/// Input needed to build a load order.
pub struct BuildInput<'a> {
    /// Active plugins in plugins.txt order.
    pub active_plugins: &'a [&'a str],
    /// For each plugin, is it ESL-flagged?
    pub is_esl: &'a dyn Fn(&str) -> bool,
}

/// Bethesda implicit ESMs loaded before plugins.txt.
pub const IMPLICIT_PLUGINS: &[&str] = &[
    "Skyrim.esm",
    "Update.esm",
    "Dawnguard.esm",
    "HearthFires.esm",
    "Dragonborn.esm",
];

/// Build a load order from (implicit plugins + active user plugins).
///
/// `implicit_present`: which of `IMPLICIT_PLUGINS` actually exist in
/// the user's Data directory (caller's responsibility to check).
pub fn build(
    implicit_present: &[&str],
    active_user_plugins: &[&str],
    is_esl: &dyn Fn(&str) -> bool,
) -> LoadOrder {
    let mut order = LoadOrder::default();
    let mut next_full: u8 = 0;
    let mut next_light: u16 = 0;

    let mut assign = |name: &str, esl: bool, order: &mut LoadOrder, nf: &mut u8, nl: &mut u16| {
        let slot = if esl {
            let s = LoadSlot::Light(*nl);
            *nl += 1;
            s
        } else {
            let s = LoadSlot::Full(*nf);
            *nf = nf.checked_add(1).expect("full-slot overflow > 0xFE");
            s
        };
        order.plugin_names.push(name.to_string());
        order.slots.insert(name.to_lowercase(), slot);
    };

    for &name in implicit_present {
        assign(name, is_esl(name), &mut order, &mut next_full, &mut next_light);
    }
    for &name in active_user_plugins {
        // Skip if already present (user listed an implicit plugin
        // in their plugins.txt — some tools do).
        if order.lookup(name).is_some() {
            continue;
        }
        assign(name, is_esl(name), &mut order, &mut next_full, &mut next_light);
    }

    order
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn full_slot_layout() {
        let order = build(
            &["Skyrim.esm", "Update.esm"],
            &["UserMod.esp"],
            &|_| false,
        );
        assert_eq!(order.lookup("Skyrim.esm"), Some(&LoadSlot::Full(0x00)));
        assert_eq!(order.lookup("Update.esm"), Some(&LoadSlot::Full(0x01)));
        assert_eq!(order.lookup("UserMod.esp"), Some(&LoadSlot::Full(0x02)));
    }

    #[test]
    fn esl_goes_to_light_pool() {
        let order = build(
            &["Skyrim.esm"],
            &["LightMod.esl", "HeavyMod.esp"],
            &|n| n.ends_with(".esl"),
        );
        assert_eq!(order.lookup("Skyrim.esm"), Some(&LoadSlot::Full(0x00)));
        assert_eq!(order.lookup("LightMod.esl"), Some(&LoadSlot::Light(0x000)));
        // HeavyMod.esp gets the NEXT full slot, which is 0x01 (light
        // doesn't consume a full slot).
        assert_eq!(order.lookup("HeavyMod.esp"), Some(&LoadSlot::Full(0x01)));
    }

    #[test]
    fn case_insensitive_lookup() {
        let order = build(&["Skyrim.esm"], &[], &|_| false);
        assert!(order.lookup("SKYRIM.ESM").is_some());
        assert!(order.lookup("skyrim.esm").is_some());
    }

    #[test]
    fn form_id_composition_full() {
        let slot = LoadSlot::Full(0x02);
        assert_eq!(slot.compose_form_id(0x0001_2EB7), 0x02_01_2E_B7);
    }

    #[test]
    fn form_id_composition_light() {
        let slot = LoadSlot::Light(0x123);
        // 0xFE << 24 | 0x123 << 12 | local & 0xFFF
        // local 0x0ABC → 0xFE_12_3A_BC
        assert_eq!(slot.compose_form_id(0x0000_0ABC), 0xFE_12_3A_BC);
        // local's high bits > 12 are truncated: 0xFFF_ABC → 0xABC
        assert_eq!(slot.compose_form_id(0x00FF_FABC), 0xFE_12_3A_BC);
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib load_order::tests
git add crates/mora-esp/src/load_order.rs
git commit -m "mora-esp: load-order resolver with ESL light slots

LoadSlot::Full(u8) | Light(u12) with compose_form_id helper.
LoadOrder indexes plugins by lowercased name. build() assigns slots
in (implicit first, user-active second) order; ESL-flagged plugins
go to the 0xFE light pool without consuming a full slot. 5 unit tests."
```

---

### Task 12: Implement master-index remapping helper

**Files:**
- Append: `crates/mora-esp/src/load_order.rs`

- [ ] **Step 1: Append remap helper + tests**

```bash
cat >> crates/mora-esp/src/load_order.rs <<'EOF'

/// Remap a plugin-local FormID (high byte = local mod index) into a
/// fully-resolved FormID using the plugin's master list + the live
/// load order.
///
/// `raw_form_id`: 32-bit FormID from a record or reference subrecord.
/// `masters`: plugin's master list (in local-index order).
/// `self_name`: the plugin's own filename (used when the local index
/// equals `masters.len()` — which means "this plugin's own forms").
/// `load_order`: the resolved runtime load order.
///
/// Returns `None` if the local index is out of range or the referenced
/// plugin is not in the load order.
pub fn remap_form_id(
    raw_form_id: u32,
    masters: &[String],
    self_name: &str,
    load_order: &LoadOrder,
) -> Option<u32> {
    let local_index = (raw_form_id >> 24) as usize;
    let local_id = raw_form_id & 0x00FF_FFFF;

    let referenced_name = if local_index < masters.len() {
        &masters[local_index]
    } else if local_index == masters.len() {
        self_name
    } else {
        return None;
    };

    let slot = load_order.lookup(referenced_name)?;
    Some(slot.compose_form_id(local_id))
}

#[cfg(test)]
mod remap_tests {
    use super::*;

    fn build_order() -> LoadOrder {
        build(
            &["Skyrim.esm", "Update.esm"],
            &["MyMod.esp", "MyLight.esl"],
            &|n| n.ends_with(".esl"),
        )
    }

    #[test]
    fn remap_via_master_zero() {
        // MyMod.esp references Skyrim.esm via local index 0.
        // raw 0x00_ABCDEF → Skyrim.esm (full slot 0x00), local 0xABCDEF
        //   → 0x00_ABCDEF
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];
        let out = remap_form_id(0x00_AB_CD_EF, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0x00_AB_CD_EF);
    }

    #[test]
    fn remap_via_master_one() {
        // Update.esm → slot 0x01, local 0xABCDEF → 0x01_ABCDEF
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];
        let out = remap_form_id(0x01_AB_CD_EF, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0x01_AB_CD_EF);
    }

    #[test]
    fn remap_self_reference() {
        // raw 0x02_XYZ with masters = [Skyrim, Update] → local index 2 = self = MyMod.esp
        // MyMod.esp is the first user plugin → full slot 0x02.
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];
        let out = remap_form_id(0x02_AB_CD_EF, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0x02_AB_CD_EF);
    }

    #[test]
    fn remap_to_esl() {
        // MyLight.esl is in the light pool at slot 0.
        let order = build_order();
        // If MyMod.esp adds MyLight.esl as a master (hypothetically),
        // then local index 2 = MyLight.esl.
        let masters = vec![
            "Skyrim.esm".to_string(),
            "Update.esm".to_string(),
            "MyLight.esl".to_string(),
        ];
        // raw 0x02_ABC with local index 2 (MyLight.esl).
        // ESL composes as FE | slot(0) << 12 | local & 0xFFF
        //   → 0xFE_00_0A_BC
        let out = remap_form_id(0x02_00_0A_BC, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0xFE_00_0A_BC);
    }

    #[test]
    fn remap_out_of_range_returns_none() {
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string()];
        // Local index 5, but only 1 master + self = valid indices are 0, 1
        assert!(remap_form_id(0x05_00_00_01, &masters, "MyMod.esp", &order).is_none());
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib load_order::remap_tests
git add crates/mora-esp/src/load_order.rs
git commit -m "mora-esp: master-index FormID remapping

remap_form_id(raw, masters, self_name, load_order) -> Option<u32>
resolves a plugin-local FormID against the plugin's master list +
live load order. Handles self-reference (local index == len(masters))
and light-slot composition. 5 unit tests."
```

---

## Phase H — EspPlugin + EspWorld (Tasks 13-14)

### Task 13: Implement `plugin.rs` — `EspPlugin` (single plugin view)

**Files:**
- Modify: `crates/mora-esp/src/plugin.rs`

- [ ] **Step 1: Write plugin.rs**

```bash
cat > crates/mora-esp/src/plugin.rs <<'EOF'
//! `EspPlugin` — a single mmapped plugin with parsed TES4 header.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use memmap2::Mmap;

use crate::tes4::{parse_tes4, Tes4Error, Tes4Header};

#[derive(Debug, thiserror::Error)]
pub enum EspPluginError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("tes4: {0}")]
    Tes4(#[from] Tes4Error),
}

/// A single loaded plugin — mmapped bytes + parsed TES4 header.
pub struct EspPlugin {
    pub path: PathBuf,
    pub filename: String,
    pub header: Tes4Header,
    /// The full mmapped byte buffer. `Arc` so slices can outlive the
    /// `EspPlugin` struct during iteration.
    pub bytes: Arc<Mmap>,
}

impl EspPlugin {
    /// Open a plugin file, mmap it, parse the TES4 header.
    pub fn open(path: &Path) -> Result<Self, EspPluginError> {
        let file = std::fs::File::open(path)?;
        // SAFETY: the file is opened read-only and the Mmap lives
        // in an Arc owned by the EspPlugin; memory is released when
        // all Arcs drop. Mora never modifies the mmap.
        let mmap = unsafe { Mmap::map(&file)? };
        let header = parse_tes4(&mmap)?;
        let filename = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("")
            .to_string();
        Ok(EspPlugin {
            path: path.to_path_buf(),
            filename,
            header,
            bytes: Arc::new(mmap),
        })
    }

    pub fn is_esm(&self) -> bool {
        self.header.is_esm()
    }

    pub fn is_esl(&self) -> bool {
        self.header.is_esl()
    }

    pub fn masters(&self) -> &[String] {
        &self.header.masters
    }

    /// The byte slice following the TES4 record — where groups begin.
    pub fn body(&self) -> &[u8] {
        // Re-parse TES4 to find where its record ends. The TES4 record
        // header is 24 bytes; data_size tells us the body length.
        use crate::record::{read_record, RECORD_HEADER_SIZE};
        // We know parse_tes4 succeeded, so read_record won't fail.
        let (_rec, next) = read_record(&self.bytes, 0).expect("tes4 header already parsed");
        let _ = RECORD_HEADER_SIZE;
        &self.bytes[next..]
    }
}

impl std::fmt::Debug for EspPlugin {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("EspPlugin")
            .field("filename", &self.filename)
            .field("is_esm", &self.is_esm())
            .field("is_esl", &self.is_esl())
            .field("masters", &self.header.masters)
            .field("byte_len", &self.bytes.len())
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn build_minimal_esp_bytes(is_esm: bool) -> Vec<u8> {
        let mut subs = Vec::new();
        subs.extend_from_slice(b"HEDR");
        subs.extend_from_slice(&12u16.to_le_bytes());
        subs.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
        subs.extend_from_slice(&0u32.to_le_bytes());
        subs.extend_from_slice(&0x800u32.to_le_bytes());

        let mut buf = Vec::new();
        buf.extend_from_slice(b"TES4");
        buf.extend_from_slice(&(subs.len() as u32).to_le_bytes());
        buf.extend_from_slice(&(if is_esm { 1u32 } else { 0u32 }).to_le_bytes());
        buf.extend_from_slice(&0u32.to_le_bytes());
        buf.extend_from_slice(&0u32.to_le_bytes());
        buf.extend_from_slice(&44u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&subs);
        buf
    }

    #[test]
    fn open_minimal_plugin() {
        let tmpdir = std::env::temp_dir().join(format!("mora-esp-test-{}", std::process::id()));
        std::fs::create_dir_all(&tmpdir).unwrap();
        let path = tmpdir.join("Test.esm");
        {
            let mut f = std::fs::File::create(&path).unwrap();
            f.write_all(&build_minimal_esp_bytes(true)).unwrap();
        }
        let plugin = EspPlugin::open(&path).unwrap();
        assert_eq!(plugin.filename, "Test.esm");
        assert!(plugin.is_esm());
        assert!(!plugin.is_esl());
        assert!(plugin.masters().is_empty());
        std::fs::remove_file(&path).ok();
        std::fs::remove_dir(&tmpdir).ok();
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib plugin::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/plugin.rs
git commit -m "mora-esp: EspPlugin — mmapped single-plugin view

EspPlugin::open(path) mmaps the file and parses the TES4 header.
Exposes filename, masters, is_esm / is_esl, and body() — the slice
after the TES4 record. Arc<Mmap> for multi-threaded access. 1 smoke
test: open a synthesized ESM file, confirm flags + masters."
```

---

### Task 14: Implement `world.rs` — `EspWorld` multi-plugin indexed view

**Files:**
- Modify: `crates/mora-esp/src/world.rs`

- [ ] **Step 1: Write world.rs**

```bash
cat > crates/mora-esp/src/world.rs <<'EOF'
//! `EspWorld` — indexed view across all active plugins.
//!
//! Iterates records by signature (e.g. all WEAP records from every
//! plugin in the load order). Handles top-level group scanning and
//! master-index FormID remapping.

use std::path::Path;

use crate::group::{read_group, GROUP_HEADER_SIZE};
use crate::load_order::{build as build_load_order, remap_form_id, LoadOrder};
use crate::plugin::{EspPlugin, EspPluginError};
use crate::plugins_txt;
use crate::reader::ReadError;
use crate::record::{read_record, Record};
use crate::signature::Signature;

#[derive(Debug, thiserror::Error)]
pub enum WorldError {
    #[error("plugin: {0}")]
    Plugin(#[from] EspPluginError),
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
}

/// A record yielded by `EspWorld::records`. Includes the owning
/// plugin index for downstream consumers that want to know provenance.
pub struct WorldRecord<'a> {
    pub plugin_index: usize,
    pub resolved_form_id: u32,
    pub record: Record<'a>,
}

pub struct EspWorld {
    pub plugins: Vec<EspPlugin>,
    pub load_order: LoadOrder,
}

impl EspWorld {
    /// Open all plugins referenced by an already-parsed plugins.txt.
    pub fn open<P: AsRef<Path>>(data_dir: P, plugins_txt_path: &Path) -> Result<Self, WorldError> {
        let data_dir = data_dir.as_ref();
        let content = std::fs::read_to_string(plugins_txt_path)?;
        let entries = plugins_txt::parse(&content);
        let active_names: Vec<&str> = entries
            .iter()
            .filter(|e| e.active)
            .map(|e| e.name.as_str())
            .collect();

        // Check which implicit plugins exist on disk.
        let implicit_present: Vec<&str> = crate::load_order::IMPLICIT_PLUGINS
            .iter()
            .copied()
            .filter(|n| data_dir.join(n).exists())
            .collect();

        // Open each plugin so we can check the ESL flag.
        let mut plugins = Vec::new();
        for name in implicit_present.iter().chain(active_names.iter()) {
            let path = data_dir.join(name);
            if !path.exists() {
                continue; // plugins.txt may list stale entries
            }
            let plugin = EspPlugin::open(&path)?;
            plugins.push(plugin);
        }

        let is_esl = |name: &str| -> bool {
            plugins
                .iter()
                .find(|p| p.filename.eq_ignore_ascii_case(name))
                .map(|p| p.is_esl())
                .unwrap_or(false)
        };
        let user_refs: Vec<&str> = active_names.clone();
        let load_order = build_load_order(&implicit_present, &user_refs, &is_esl);

        Ok(EspWorld {
            plugins,
            load_order,
        })
    }

    /// Iterate records of the given signature across all active
    /// plugins, in load order. FormIDs are remapped to live values.
    pub fn records(&self, sig: Signature) -> impl Iterator<Item = WorldRecord<'_>> + '_ {
        self.plugins.iter().enumerate().flat_map(move |(idx, p)| {
            scan_top_level_group(p, sig).map(move |r| {
                let resolved = remap_form_id(
                    r.form_id,
                    &p.header.masters,
                    &p.filename,
                    &self.load_order,
                )
                .unwrap_or(r.form_id);
                WorldRecord {
                    plugin_index: idx,
                    resolved_form_id: resolved,
                    record: r,
                }
            })
        })
    }
}

fn scan_top_level_group<'a>(
    plugin: &'a EspPlugin,
    target: Signature,
) -> Box<dyn Iterator<Item = Record<'a>> + 'a> {
    let body = plugin.body();
    // Walk top-level groups looking for the target.
    let mut offset = 0usize;
    let mut found_contents: Option<&'a [u8]> = None;
    while offset < body.len() {
        let (group, next) = match read_group(body, offset) {
            Ok(x) => x,
            Err(_) => break,
        };
        if group.is_top_level() && group.label_signature() == target {
            found_contents = Some(group.contents);
            break;
        }
        offset = next;
    }

    let Some(contents) = found_contents else {
        return Box::new(std::iter::empty());
    };

    Box::new(GroupRecordIter {
        bytes: contents,
        offset: 0,
    })
}

struct GroupRecordIter<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Iterator for GroupRecordIter<'a> {
    type Item = Record<'a>;
    fn next(&mut self) -> Option<Record<'a>> {
        while self.offset + 4 <= self.bytes.len() {
            // A nested group might appear inside a top-level group
            // (e.g. CELL contains children). Skip past it.
            if &self.bytes[self.offset..self.offset + 4] == b"GRUP" {
                if self.offset + 8 > self.bytes.len() {
                    return None;
                }
                let size = u32::from_le_bytes(
                    self.bytes[self.offset + 4..self.offset + 8].try_into().unwrap(),
                ) as usize;
                self.offset += size.max(GROUP_HEADER_SIZE);
                continue;
            }
            match read_record(self.bytes, self.offset) {
                Ok((rec, next)) => {
                    self.offset = next;
                    return Some(rec);
                }
                Err(_) => return None,
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    // Full EspWorld integration tests live in tests/esp_format.rs
    // (Task 21); here we just confirm the struct compiles.
    #[test]
    fn compiles() {}
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib world::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/world.rs
git commit -m "mora-esp: EspWorld multi-plugin indexed view

EspWorld::open(data_dir, plugins_txt) -> World. records(signature)
iterates all records of that type across plugins in load order,
with resolved FormIDs. Nested groups are skipped. Integration tests
land in tests/esp_format.rs (Task 21)."
```

---

## Phase I — Replace mora-core placeholder (Task 15)

### Task 15: Wire `EspWorld` into `mora-core::distributor`

**Files:**
- Modify: `crates/mora-core/Cargo.toml` — DO NOT add mora-esp dep (would cycle)
- Modify: `crates/mora-core/src/distributor.rs` — keep `EspWorld` as a type alias that frontends use via a generic

**Problem:** `mora-core::Distributor::lower` takes `&EspWorld`. If we replace the placeholder with a re-export from `mora-esp`, we create `mora-core → mora-esp → mora-core` which is a cycle.

**Solution:** keep `mora-core::EspWorld` as an opaque placeholder struct. Change the `Distributor` trait to be generic over the world type. Each frontend (mora-kid, etc.) instantiates with the real `mora_esp::EspWorld`. mora-cli ties it together without introducing a cycle.

- [ ] **Step 1: Update the trait to be generic**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-core/src/distributor.rs <<'EOF'
//! The `Distributor` trait — the extensibility hinge for future
//! frontends (mora-kid, mora-spid, mora-skypatcher, …).
//!
//! `Distributor` is generic over the world type so we avoid a
//! `mora-core → mora-esp → mora-core` cycle. Frontends bind the
//! generic to `mora_esp::EspWorld`; `mora-cli` ties everything
//! together.

use crate::chance::DeterministicChance;
use crate::patch_sink::PatchSink;

/// Per-run statistics summed across all registered frontends.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats {
    pub rules_evaluated: u64,
    pub candidates_considered: u64,
    pub patches_emitted: u64,
    pub rejected_by_chance: u64,
    pub rejected_by_filter: u64,
}

impl std::ops::AddAssign for DistributorStats {
    fn add_assign(&mut self, rhs: Self) {
        self.rules_evaluated += rhs.rules_evaluated;
        self.candidates_considered += rhs.candidates_considered;
        self.patches_emitted += rhs.patches_emitted;
        self.rejected_by_chance += rhs.rejected_by_chance;
        self.rejected_by_filter += rhs.rejected_by_filter;
    }
}

/// A distributor frontend — consumes ESP + rules → produces patches.
///
/// Generic over `World` so `mora-core` doesn't depend on `mora-esp`.
/// Frontends (mora-kid, etc.) instantiate with
/// `mora_esp::EspWorld` in their `Distributor` impl.
pub trait Distributor<World: ?Sized> {
    type Error: std::error::Error + Send + Sync + 'static;

    fn name(&self) -> &'static str;

    fn lower(
        &self,
        world: &World,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error>;
}

/// Placeholder kept for backward compatibility with Plan 4 code.
/// New code should use the real `mora_esp::EspWorld`.
#[deprecated(note = "use mora_esp::EspWorld directly")]
pub struct EspWorld;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stats_add_assign() {
        let mut a = DistributorStats {
            rules_evaluated: 1,
            candidates_considered: 2,
            patches_emitted: 3,
            rejected_by_chance: 4,
            rejected_by_filter: 5,
        };
        let b = DistributorStats {
            rules_evaluated: 10,
            candidates_considered: 20,
            patches_emitted: 30,
            rejected_by_chance: 40,
            rejected_by_filter: 50,
        };
        a += b;
        assert_eq!(
            a,
            DistributorStats {
                rules_evaluated: 11,
                candidates_considered: 22,
                patches_emitted: 33,
                rejected_by_chance: 44,
                rejected_by_filter: 55,
            }
        );
    }
}
EOF
```

- [ ] **Step 2: Update mora-core re-exports — drop the placeholder**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-core/src/lib.rs")
text = p.read_text()
old = "pub use distributor::{Distributor, DistributorStats, EspWorld};"
new = "pub use distributor::{Distributor, DistributorStats};"
text = text.replace(old, new)
p.write_text(text)
PY
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo test --workspace
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-core/src/distributor.rs crates/mora-core/src/lib.rs
git commit -m "mora-core: make Distributor trait generic over World

Plan 5's EspWorld placeholder becomes real in mora-esp, but adding
a mora-core -> mora-esp dep would cycle. Generic World param lets
frontends bind to mora_esp::EspWorld without the cycle. Old EspWorld
type retained with #[deprecated]; removed from crate re-exports."
```

---

## Phase J — Subrecord parsers (Tasks 16-17)

### Task 16: Implement `subrecords/edid.rs` — editor ID

**Files:**
- Modify: `crates/mora-esp/src/subrecords/mod.rs`
- Modify: `crates/mora-esp/src/subrecords/edid.rs`

- [ ] **Step 1: Write mod.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/src/subrecords/mod.rs <<'EOF'
//! Typed subrecord parsers.
//!
//! Each module exposes a `parse(&[u8]) -> Result<T, Err>` function.
//! Callers obtain subrecord bytes from `SubrecordIter` and pass to
//! the typed parser.

pub mod edid;
pub mod kwda;
EOF
```

- [ ] **Step 2: Write edid.rs**

```bash
cat > crates/mora-esp/src/subrecords/edid.rs <<'EOF'
//! `EDID` — editor ID (NUL-terminated ASCII string).

use crate::reader::{read_cstr, ReadError};

/// Parse an EDID subrecord payload into an owned string.
pub fn parse(data: &[u8]) -> Result<String, ReadError> {
    let (s, _) = read_cstr(data, 0, data.len())?;
    Ok(s.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_nul_terminated() {
        let data = b"IronSword\0";
        assert_eq!(parse(data).unwrap(), "IronSword");
    }

    #[test]
    fn handles_empty_string() {
        let data = b"\0";
        assert_eq!(parse(data).unwrap(), "");
    }

    #[test]
    fn missing_nul_errors() {
        let data = b"NoNul";
        assert!(parse(data).is_err());
    }
}
EOF
```

- [ ] **Step 3: Verify + commit**

```bash
cargo test --package mora-esp --lib subrecords::edid::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/subrecords/
git commit -m "mora-esp: subrecords::edid — editor ID parser

parse(data) -> Result<String, ReadError> reads a NUL-terminated
ASCII string. 3 unit tests: happy, empty, missing NUL errors."
```

---

### Task 17: Implement `subrecords/kwda.rs` — keyword array

**Files:**
- Modify: `crates/mora-esp/src/subrecords/kwda.rs`

- [ ] **Step 1: Write kwda.rs**

```bash
cat > crates/mora-esp/src/subrecords/kwda.rs <<'EOF'
//! `KWDA` — keyword form-id array.
//!
//! Raw layout: sequence of 32-bit LE FormIDs (local — needs remapping
//! via the containing plugin's master list).

use crate::reader::{le_u32, ReadError};

/// Parse a KWDA payload into a `Vec<u32>` of local FormIDs.
pub fn parse(data: &[u8]) -> Result<Vec<u32>, ReadError> {
    if data.len() % 4 != 0 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 4 - (data.len() % 4),
        });
    }
    let mut out = Vec::with_capacity(data.len() / 4);
    let mut o = 0;
    while o < data.len() {
        let (id, next) = le_u32(data, o)?;
        out.push(id);
        o = next;
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_three_ids() {
        let data = [
            0x11u8, 0x22, 0x33, 0x44, // 0x44332211
            0xAA, 0xBB, 0xCC, 0xDD, // 0xDDCCBBAA
            0x00, 0x00, 0x00, 0x00, // 0x00000000
        ];
        let ids = parse(&data).unwrap();
        assert_eq!(ids, vec![0x44332211, 0xDDCCBBAA, 0x00000000]);
    }

    #[test]
    fn empty_payload_is_ok() {
        let ids = parse(&[]).unwrap();
        assert!(ids.is_empty());
    }

    #[test]
    fn unaligned_payload_errors() {
        let data = [0u8, 1, 2]; // 3 bytes, not a multiple of 4
        assert!(parse(&data).is_err());
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib subrecords::kwda::tests
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/subrecords/kwda.rs
git commit -m "mora-esp: subrecords::kwda — keyword form-id array

parse(data) -> Vec<u32> reads LE u32 FormIDs. Rejects payloads not a
multiple of 4 bytes. 3 unit tests: 3-id happy, empty, unaligned error.
IDs are local — callers remap via the plugin's master list."
```

---

## Phase K — Record-type accessors (Tasks 18-19)

### Task 18: Implement `records/weapon.rs`

**Files:**
- Modify: `crates/mora-esp/src/records/mod.rs`
- Modify: `crates/mora-esp/src/records/weapon.rs`

- [ ] **Step 1: Write records/mod.rs**

```bash
cat > crates/mora-esp/src/records/mod.rs <<'EOF'
//! Typed record-body accessors.
//!
//! Each module takes a `Record<'a>` and exposes named fields parsed
//! from the subrecord stream, handling compressed records transparently.

pub mod armor;
pub mod weapon;
EOF
```

- [ ] **Step 2: Write records/weapon.rs**

```bash
cat > crates/mora-esp/src/records/weapon.rs <<'EOF'
//! `WEAP` — weapon record accessor.
//!
//! M2 exposes the subset mora-kid needs:
//! - `editor_id` (EDID subrecord)
//! - `keywords` (KWDA subrecord — local FormIDs)
//!
//! Fields not yet exposed (added when consumers need them):
//! DNAM (damage, weight, value, reach, speed), NNAM (sound), etc.

use crate::compression::{decompress, DecompressError};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{EDID, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::{edid, kwda};

#[derive(Debug, thiserror::Error)]
pub enum WeaponError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

/// Parsed WEAP record.
#[derive(Debug, Default)]
pub struct WeaponRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<u32>, // local FormIDs; caller remaps
}

/// Parse a WEAP record's body (handling compression).
pub fn parse(record: &Record<'_>) -> Result<WeaponRecord, WeaponError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut w = WeaponRecord::default();
    let mut iter = SubrecordIter::new(body);
    while let Some(sub) = iter.next() {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => w.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => w.keywords = kwda::parse(sub.data)?,
            _ => {} // other subrecords ignored at M2
        }
    }
    Ok(w)
}

#[cfg(test)]
mod tests {
    // Full WEAP parsing tests land in tests/esp_format.rs (Task 21).
}
EOF
```

- [ ] **Step 3: Verify + commit**

```bash
cargo test --package mora-esp --lib records::weapon
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/records/
git commit -m "mora-esp: records::weapon — WEAP record accessor

parse(record) -> WeaponRecord { editor_id, keywords }. Handles
compressed records (LZ4 inflation). Keywords are local FormIDs —
caller remaps via plugin's master list. Full-stack tests in
tests/esp_format.rs."
```

---

### Task 19: Implement `records/armor.rs`

**Files:**
- Modify: `crates/mora-esp/src/records/armor.rs`

- [ ] **Step 1: Write armor.rs**

Note: `ARMO` and `WEAP` have nearly identical shape at the EDID+KWDA level — code duplication is fine here since per-type struct evolves independently as mora-kid adds trait filters.

```bash
cat > crates/mora-esp/src/records/armor.rs <<'EOF'
//! `ARMO` — armor record accessor.
//!
//! M2 exposes the subset mora-kid needs:
//! - `editor_id` (EDID subrecord)
//! - `keywords` (KWDA subrecord — local FormIDs)

use crate::compression::{decompress, DecompressError};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{EDID, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::{edid, kwda};

#[derive(Debug, thiserror::Error)]
pub enum ArmorError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

/// Parsed ARMO record.
#[derive(Debug, Default)]
pub struct ArmorRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<u32>,
}

pub fn parse(record: &Record<'_>) -> Result<ArmorRecord, ArmorError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut a = ArmorRecord::default();
    let mut iter = SubrecordIter::new(body);
    while let Some(sub) = iter.next() {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => a.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => a.keywords = kwda::parse(sub.data)?,
            _ => {}
        }
    }
    Ok(a)
}

#[cfg(test)]
mod tests {
    // Full ARMO parsing tests land in tests/esp_format.rs (Task 21).
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-esp --lib records::armor
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc
git add crates/mora-esp/src/records/armor.rs
git commit -m "mora-esp: records::armor — ARMO record accessor

parse(record) -> ArmorRecord { editor_id, keywords }. Mirrors
weapon.rs; the two share no code to let per-type struct evolve
independently."
```

---

## Phase L — Integration tests (Tasks 20-21)

### Task 20: Shared fixture-building helpers

**Files:**
- Create: `crates/mora-esp/tests/fixtures.rs`

- [ ] **Step 1: Write the fixtures module**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-esp/tests/fixtures.rs <<'EOF'
//! Shared fixture builders for mora-esp integration tests.
//!
//! Composes ESP byte buffers in memory via builder structs. Designed
//! for readable test bodies that focus on what's being asserted.

pub const TES4_ESM_FLAG: u32 = 0x0000_0001;
pub const TES4_ESL_FLAG: u32 = 0x0000_0200;

/// Builder for a subrecord (signature + LE u16 size + payload).
pub struct SubrecordBuilder {
    pub signature: [u8; 4],
    pub payload: Vec<u8>,
}

impl SubrecordBuilder {
    pub fn new(sig: &[u8; 4], payload: Vec<u8>) -> Self {
        SubrecordBuilder {
            signature: *sig,
            payload,
        }
    }

    pub fn bytes(&self) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(&self.signature);
        v.extend_from_slice(&(self.payload.len() as u16).to_le_bytes());
        v.extend_from_slice(&self.payload);
        v
    }
}

/// Helper: EDID payload — NUL-terminated string.
pub fn edid_payload(id: &str) -> Vec<u8> {
    let mut v = id.as_bytes().to_vec();
    v.push(0);
    v
}

/// Helper: KWDA payload — concatenated LE u32 FormIDs.
pub fn kwda_payload(ids: &[u32]) -> Vec<u8> {
    let mut v = Vec::with_capacity(ids.len() * 4);
    for &id in ids {
        v.extend_from_slice(&id.to_le_bytes());
    }
    v
}

/// Builder for a record (24-byte header + subrecords).
pub struct RecordBuilder {
    pub signature: [u8; 4],
    pub flags: u32,
    pub form_id: u32,
    pub subrecords: Vec<SubrecordBuilder>,
}

impl RecordBuilder {
    pub fn new(sig: &[u8; 4], form_id: u32) -> Self {
        RecordBuilder {
            signature: *sig,
            flags: 0,
            form_id,
            subrecords: Vec::new(),
        }
    }

    pub fn flag(mut self, flag: u32) -> Self {
        self.flags |= flag;
        self
    }

    pub fn add(mut self, sub: SubrecordBuilder) -> Self {
        self.subrecords.push(sub);
        self
    }

    pub fn bytes(&self) -> Vec<u8> {
        let mut body = Vec::new();
        for sub in &self.subrecords {
            body.extend_from_slice(&sub.bytes());
        }
        let mut v = Vec::new();
        v.extend_from_slice(&self.signature);
        v.extend_from_slice(&(body.len() as u32).to_le_bytes());
        v.extend_from_slice(&self.flags.to_le_bytes());
        v.extend_from_slice(&self.form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&44u16.to_le_bytes()); // version
        v.extend_from_slice(&0u16.to_le_bytes()); // unknown
        v.extend_from_slice(&body);
        v
    }
}

/// Builder for a top-level group (24-byte header + contained records).
pub struct GroupBuilder {
    pub label: [u8; 4],
    pub records: Vec<RecordBuilder>,
}

impl GroupBuilder {
    pub fn new(label: &[u8; 4]) -> Self {
        GroupBuilder {
            label: *label,
            records: Vec::new(),
        }
    }

    pub fn add(mut self, rec: RecordBuilder) -> Self {
        self.records.push(rec);
        self
    }

    pub fn bytes(&self) -> Vec<u8> {
        let mut contents = Vec::new();
        for r in &self.records {
            contents.extend_from_slice(&r.bytes());
        }
        let total_size = 24 + contents.len();
        let mut v = Vec::new();
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&(total_size as u32).to_le_bytes());
        v.extend_from_slice(&self.label);
        v.extend_from_slice(&0u32.to_le_bytes()); // group_type = 0
        v.extend_from_slice(&0u16.to_le_bytes()); // timestamp
        v.extend_from_slice(&0u16.to_le_bytes()); // vc_info
        v.extend_from_slice(&0u32.to_le_bytes()); // unknown
        v.extend_from_slice(&contents);
        v
    }
}

/// Builder for a full plugin: TES4 header + groups.
pub struct PluginBuilder {
    pub tes4_flags: u32,
    pub masters: Vec<String>,
    pub groups: Vec<GroupBuilder>,
}

impl PluginBuilder {
    pub fn new() -> Self {
        PluginBuilder {
            tes4_flags: 0,
            masters: Vec::new(),
            groups: Vec::new(),
        }
    }

    pub fn esm(mut self) -> Self {
        self.tes4_flags |= TES4_ESM_FLAG;
        self
    }

    pub fn esl(mut self) -> Self {
        self.tes4_flags |= TES4_ESL_FLAG;
        self
    }

    pub fn master(mut self, name: &str) -> Self {
        self.masters.push(name.to_string());
        self
    }

    pub fn add_group(mut self, g: GroupBuilder) -> Self {
        self.groups.push(g);
        self
    }

    pub fn bytes(&self) -> Vec<u8> {
        // TES4 header
        let mut tes4_body = Vec::new();
        // HEDR
        tes4_body.extend_from_slice(b"HEDR");
        tes4_body.extend_from_slice(&12u16.to_le_bytes());
        tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
        tes4_body.extend_from_slice(&0u32.to_le_bytes());
        tes4_body.extend_from_slice(&0x800u32.to_le_bytes());
        // Masters
        for m in &self.masters {
            let name_bytes = m.as_bytes();
            tes4_body.extend_from_slice(b"MAST");
            tes4_body.extend_from_slice(&((name_bytes.len() + 1) as u16).to_le_bytes());
            tes4_body.extend_from_slice(name_bytes);
            tes4_body.push(0);
            tes4_body.extend_from_slice(b"DATA");
            tes4_body.extend_from_slice(&8u16.to_le_bytes());
            tes4_body.extend_from_slice(&0u64.to_le_bytes());
        }

        let mut v = Vec::new();
        v.extend_from_slice(b"TES4");
        v.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
        v.extend_from_slice(&self.tes4_flags.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // form_id
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&44u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&tes4_body);

        for g in &self.groups {
            v.extend_from_slice(&g.bytes());
        }
        v
    }
}

impl Default for PluginBuilder {
    fn default() -> Self {
        Self::new()
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

This file is a library-style test helper (not a test module); Rust sees it as a separate integration test binary. Add a trivial test so cargo test picks it up and confirms it compiles.

```bash
cat >> crates/mora-esp/tests/fixtures.rs <<'EOF'

#[test]
fn fixtures_compile() {
    let _ = PluginBuilder::new().esm().master("Skyrim.esm").bytes();
}
EOF
```

```bash
cargo test --package mora-esp --test fixtures
git add crates/mora-esp/tests/fixtures.rs
git commit -m "mora-esp: test-fixture builders

PluginBuilder / GroupBuilder / RecordBuilder / SubrecordBuilder +
edid_payload / kwda_payload helpers. Composes ESP byte buffers
inline — reused by esp_format.rs integration tests."
```

---

### Task 21: End-to-end integration tests

**Files:**
- Create: `crates/mora-esp/tests/esp_format.rs`

- [ ] **Step 1: Write the test file**

```bash
cat > crates/mora-esp/tests/esp_format.rs <<'EOF'
//! End-to-end integration tests for mora-esp.
//!
//! Builds synthetic plugins via the `fixtures` module, writes to
//! tmp files, opens through EspPlugin / EspWorld, and asserts.

mod fixtures;

use std::io::Write;
use std::path::PathBuf;

use fixtures::*;
use mora_esp::records::{armor, weapon};
use mora_esp::signature::{Signature, ARMO, WEAP};
use mora_esp::{EspPlugin, EspWorld};

fn write_tmp(name: &str, bytes: &[u8]) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-esp-it-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    let mut f = std::fs::File::create(&path).unwrap();
    f.write_all(bytes).unwrap();
    path
}

#[test]
fn parses_single_plugin_with_weapon() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_2EB7)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("IronSword")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0001_E718]))),
            ),
        )
        .bytes();

    let path = write_tmp("TestPlugin.esm", &plugin_bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    assert!(plugin.is_esm());
    assert_eq!(plugin.filename, "TestPlugin.esm");

    // Manually walk the body to find the WEAP group and iterate.
    // (Full EspWorld end-to-end is tested below.)
}

#[test]
fn esp_world_iterates_weapons_across_plugins() {
    let bytes_a = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_AAAA)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("SwordA")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0001_E718]))),
            ),
        )
        .bytes();
    let bytes_b = PluginBuilder::new()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_BBBB)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("SwordB")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0002_E718]))),
            ),
        )
        .bytes();

    let dir = std::env::temp_dir().join(format!("mora-esp-world-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path_a = dir.join("A.esm");
    let path_b = dir.join("B.esp");
    std::fs::write(&path_a, &bytes_a).unwrap();
    std::fs::write(&path_b, &bytes_b).unwrap();

    let plugins_txt = dir.join("plugins.txt");
    std::fs::write(&plugins_txt, "*A.esm\n*B.esp\n").unwrap();

    let world = EspWorld::open(&dir, &plugins_txt).unwrap();
    assert!(world.plugins.len() >= 2, "expected A.esm + B.esp; got {} plugins", world.plugins.len());

    let weapons: Vec<_> = world.records(WEAP).collect();
    assert!(weapons.iter().any(|w| w.record.form_id == 0x0001_AAAA));
    assert!(weapons.iter().any(|w| w.record.form_id == 0x0001_BBBB));
}

#[test]
fn weapon_parse_extracts_editor_id_and_keywords() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_2EB7)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("IronSword")))
                    .add(SubrecordBuilder::new(
                        b"KWDA",
                        kwda_payload(&[0x0001_E718, 0x0002_1234]),
                    )),
            ),
        )
        .bytes();

    let path = write_tmp("IronPlugin.esm", &plugin_bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-iron.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    for w in world.records(WEAP) {
        let parsed = weapon::parse(&w.record).unwrap();
        assert_eq!(parsed.editor_id.as_deref(), Some("IronSword"));
        assert_eq!(parsed.keywords, vec![0x0001_E718, 0x0002_1234]);
    }
}

#[test]
fn armor_parse_smoke() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"ARMO").add(
                RecordBuilder::new(b"ARMO", 0x0001_CCCC)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("IronHelmet")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0001_E719]))),
            ),
        )
        .bytes();

    let path = write_tmp("ArmorPlugin.esm", &plugin_bytes);
    let _plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-armor.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", path.file_name().unwrap().to_str().unwrap())).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    for a in world.records(ARMO) {
        let parsed = armor::parse(&a.record).unwrap();
        assert_eq!(parsed.editor_id.as_deref(), Some("IronHelmet"));
        assert_eq!(parsed.keywords, vec![0x0001_E719]);
    }
}
EOF
```

- [ ] **Step 2: Run + commit**

```bash
cargo test --package mora-esp --test esp_format
cargo xwin check --package mora-esp --target x86_64-pc-windows-msvc --tests
git add crates/mora-esp/tests/esp_format.rs
git commit -m "mora-esp: end-to-end integration tests

4 tests using synthetic plugins written to tmp: open single plugin,
EspWorld iterates across A.esm + B.esp, WEAP parse extracts EDID +
KWDA, ARMO smoke. Exercises full stack: mmap → TES4 parse →
plugins.txt parse → load order → group scanning → record iteration
→ subrecord parsing."
```

---

## Phase M — Final verification (Task 22)

### Task 22: Full clean verify + push + PR

**Files:** none modified.

- [ ] **Step 1: Clean verification**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets 2>&1 | grep -E "^test result" | awk '{count+=$4} END {print "TOTAL PASSED:", count}'
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all six green. Test count: ~72 (M2 Part 1) + ~35 new mora-esp tests = ~107 total.

- [ ] **Step 2: Push + open PR**

```bash
git push -u origin m2-mora-esp-foundation
gh pr create --base master --head m2-mora-esp-foundation \
    --title "Rust + KID pivot — M2 Part 2: mora-esp" \
    --body "$(cat <<'PRBODY'
## Summary

Delivers `mora-esp` end-to-end per
`docs/superpowers/plans/2026-04-21-rust-kid-pivot-plan-5-mora-esp.md`:

- **Byte-reader primitives + Signature type** (signature.rs, reader.rs).
- **Header parsers** — Record (24 bytes), Group (24 bytes), Subrecord (6 bytes, with XXXX override).
- **LZ4 Frame decompression** for the 0x00040000 compressed-record flag.
- **TES4 file-header parser** — HEDR + MAST + CNAM + SNAM; ESM / ESL flag detection.
- **plugins.txt parser** — CRLF-tolerant, comment-aware, active-marker `*`.
- **Load-order resolver** — implicit Bethesda ESMs + user plugins; 0x00-0xFD full-slot pool + 0xFE light-slot sub-indexing.
- **Master-index FormID remapping** — local → resolved FormID.
- **EspPlugin** — mmap a plugin, parse TES4.
- **EspWorld** — iterate records of a given signature across all active plugins in load order, with resolved FormIDs.
- **Subrecord parsers** — EDID (editor ID), KWDA (keyword FormID array).
- **Record-type accessors** — WeaponRecord, ArmorRecord (editor_id + keywords; more fields added per consumer need).
- **mora-core breaking change** — Distributor trait is now generic over `World` to avoid a mora-core↔mora-esp cycle.
- **`docs/src/mora-esp-reference.md`** — ESP binary format spec, plugins.txt spec, load-order rules.

## Test plan

- [x] `cargo test --workspace` — ~107 tests (M2 Part 1's 72 + ~35 new).
- [x] `cargo clippy --all-targets -- -D warnings` clean.
- [x] `cargo fmt --check` clean.
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean.
- [ ] Self-hosted `skyrim-integration` still red until Unraid runner image is refreshed.

## Scope discipline

- **Target: SSE / AE only** — LE/Oldrim not supported (zlib vs LZ4 difference).
- **Subrecord parsers at M2**: EDID + KWDA. Rare-trait subrecords (DNAM flags for PLAYABLE/UNIQUE, etc.) land when Plan 6 or 7's mora-kid starts hitting them.
- **Record types at M2**: Weapon + Armor. Other 18 record types from the KID compatibility matrix land per-type as Plan 6+ grows coverage.
- **No mora-kid code in this plan.** That's Plan 6.

## Next up

**Plan 6: mora-kid MVP** — KID INI parser, filter AST, Weapon + Armor distributor frontend, first end-to-end compile producing `mora_patches.bin`.
PRBODY
)"
```

- [ ] **Step 3: Watch CI + hand off**

```bash
gh run watch --exit-status 2>&1 | tail -8
```

---

## Completion criteria

- [ ] All new unit + integration tests pass; total workspace test count ~107.
- [ ] `cargo clippy -D warnings` clean.
- [ ] PR merged to `master`.
- [ ] `mora-core::distributor::Distributor` is generic over `World`; old `EspWorld` placeholder is `#[deprecated]`.

## Next plan

**Plan 6: `mora-kid` MVP** — KID INI parser, filter AST (form/keyword/string/trait/chance), Distributor impl for Weapon + Armor, first end-to-end `mora compile` producing a valid `mora_patches.bin`. Replaces `mora-cli` stub with the real CLI pipeline.
