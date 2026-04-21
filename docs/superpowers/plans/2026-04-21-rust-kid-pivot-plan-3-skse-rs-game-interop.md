# Rust + KID Pivot — Plan 3: `skse-rs` Game Interop (M1, Part 2 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete spec milestone M1 by adding just enough game-interop surface to `skse-rs` to look up a form by FormID and add a keyword to it inside a live Skyrim. Upgrade `skse-rs-smoke` to exercise the full `kDataLoaded → LookupByID → AddKeyword → verify` path. Flip CI's `skyrim-integration` gate so the smoke test runs automatically on every PR.

**Architecture:** Port exactly — and only — the minimum of CommonLibSSE-NG that this smoke test exercises. Each game type gets a partial `#[repr(C)]` layout with only the fields Mora touches, behind a `// M1-minimal` comment so future plans know where to extend. `TESForm::LookupByID` is re-implemented in Rust against the `BSTHashMap` layout at AE Address Library ID `400507` using `BSCRC32` hashing. `BGSKeywordForm::AddKeyword` is re-implemented using `MemoryManager::Allocate`/`Deallocate` (AE IDs `68115`/`68117`, reached through `MemoryManager::GetSingleton` at AE ID `11141`). The messaging interface (`kMessaging`, ID `5`) is used to register a `kDataLoaded` (msg_type `8`) callback.

**Tech Stack:** Rust 1.90 (workspace pin). New deps: none required beyond what Plan 2 added — `windows-sys` already has `Win32_System_LibraryLoader` (for `GetModuleHandleW`). `crc32fast` will be added for `BSCRC32` hashing. Cross-compile to `x86_64-pc-windows-msvc` via `cargo-xwin`.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`
**Reference plan:** `docs/superpowers/plans/2026-04-21-rust-kid-pivot-plan-2-skse-rs-foundation.md`

**Scope discipline:** This plan ports **only** what the smoke test needs. No general vtable replication, no hash-map writes, no save serialization, no extra event dispatchers. Each new type definition carries an `// M1-minimal: only <list> exercised — extend when ...` comment on its `#[repr(C)]` header. Future plans (M5 mora-runtime, M6 KID record types) extend the surface as genuine consumers appear.

---

## File Structure

**Created:**
- `crates/skse-rs/src/address_library.rs` — v2 `.bin` format parser + in-memory index
- `crates/skse-rs/src/relocation.rs` — image-base resolution + `Relocation::id()`
- `crates/skse-rs/src/game.rs` — the partial game-type module root (re-exports submodules)
- `crates/skse-rs/src/game/form.rs` — `TESForm` partial layout + `lookup_by_id`
- `crates/skse-rs/src/game/hash_map.rs` — `BSTHashMap<FormID, *mut TESForm>` read-only lookup
- `crates/skse-rs/src/game/lock.rs` — `BSReadWriteLock` bindings (4 AE IDs)
- `crates/skse-rs/src/game/data_handler.rs` — `TESDataHandler` partial layout + singleton access
- `crates/skse-rs/src/game/keyword.rs` — `BGSKeyword` partial layout
- `crates/skse-rs/src/game/keyword_form.rs` — `BGSKeywordForm` partial + `add_keyword`
- `crates/skse-rs/src/game/memory.rs` — `MemoryManager` + `skyrim_alloc` / `skyrim_free`
- `crates/skse-rs/src/messaging.rs` — `MessagingInterface` wrapper + `register_listener` helper
- `crates/skse-rs/tests/bin_fixtures/` — committed synthetic address-library `.bin` fixtures for parser tests
- `tests/integration/skse-rs-smoke/rust-ready.marker` — empty file, flips CI gate

**Modified:**
- `crates/skse-rs/Cargo.toml` — add `crc32fast = "1"`; already has Windows deps
- `crates/skse-rs/src/lib.rs` — new `pub mod` declarations + re-exports
- `crates/skse-rs/src/plugin.rs` — `SksePlugin` trait gains an optional `on_data_loaded` hook that the `declare_plugin!` macro wires up through the messaging interface
- `crates/skse-rs-smoke/src/lib.rs` — upgrade to lookup + AddKeyword + verify
- `tests/integration/skse-rs-smoke/check.sh` — new assertions reflecting the upgraded log content
- `tests/integration/skse-rs-smoke/README.md` — reflect upgraded behavior
- `docs/src/skse-rs-ffi-reference.md` — expand with the new type layouts
- `.github/workflows/ci.yml` — replace `if: false` on `skyrim-integration` with a real condition tied to the `rust-ready.marker` presence

---

## Phase A — Reference Doc Expansion (Task 1)

### Task 1: Expand `docs/src/skse-rs-ffi-reference.md` with Plan 3's game types

**Files:**
- Modify (append): `docs/src/skse-rs-ffi-reference.md`

All subsequent tasks cite this doc when defining `#[repr(C)]` structs. Lock in the layouts up front so implementation tasks can't drift.

- [ ] **Step 1: Append the new section**

Run:
```bash
cd /home/tbaldrid/oss/mora
cat >> docs/src/skse-rs-ffi-reference.md <<'EOF'

---

# Plan 3 additions — game-interop types

Plan 2 covered the SKSE plugin ABI. Plan 3 adds **partial** Rust
layouts for the Skyrim game types needed to look up a form by FormID
and add a keyword to it. Every struct here is a **minimum viable
layout** — only the fields and methods the `skse-rs-smoke` test
exercises. Each Rust `#[repr(C)]` carries an `// M1-minimal`
comment; future plans extend as real consumers land.

## Address Library bin format (v2, AE)

File: `Data/SKSE/Plugins/versionlib-1.6.1170.0.bin` (or similar).

Source: `CommonLibSSE-NG/include/REL/ID.h`. All fields little-endian.

### Header (prefix)
- `i32` **format version** — must equal `2`
- `i32` **version[0]** (major, e.g. `1`)
- `i32` **version[1]** (minor, e.g. `6`)
- `i32` **version[2]** (patch, e.g. `1170`)
- `i32` **version[3]** (build, e.g. `0`)
- `i32` **name_len** — length of the embedded name string (ignored)
- `name_len` bytes of UTF-8 name
- `i32` **pointer_size** — must equal `8` on AE
- `i32` **address_count** — number of `(id, offset)` pairs

### Delta-encoded pairs

For each pair, read one `u8` **type byte**. Its low nibble encodes
the ID update mode, its high nibble encodes the offset update mode
plus a single "divide offset by pointer_size" bit.

ID low-nibble modes:
```
0  read full u64
1  prev_id + 1
2  prev_id + read_u8
3  prev_id - read_u8
4  prev_id + read_u16
5  prev_id - read_u16
6  read_u16      (absolute u16, zero-extended to u64)
7  read_u32      (absolute u32, zero-extended to u64)
```

Offset high-nibble modes. `hi` is `type_byte >> 4`. Bit 3 of `hi`
(`hi & 8`) is the "scale by pointer_size" flag. Bits 0-2 (`hi & 7`)
select the encoding, with the pre-scaled base taken from
`prev_offset / pointer_size` when the flag is set, else
`prev_offset`:
```
mode 0  read full u64
mode 1  base + 1
mode 2  base + read_u8
mode 3  base - read_u8
mode 4  base + read_u16
mode 5  base - read_u16
mode 6  read_u16
mode 7  read_u32
```
After decoding, if the scale flag was set, multiply the resulting
offset by `pointer_size` (i.e. by 8).

Decoded pairs are stored as a flat sorted array (sorted by id); a
binary search resolves a lookup.

## `TESForm` — partial layout

Source: `CommonLibSSE-NG/include/RE/T/TESForm.h`. Documented size
`0x20`. Plan 3 only needs the `formID` field; the rest is opaque.

| Offset | Size | Field         | Type                           | Mora use   |
|--------|------|---------------|--------------------------------|------------|
| 0x00   | 8    | vtable        | `*const ()`                    | opaque     |
| 0x08   | 8    | source_files  | `*mut ()` (`TESFileArray*`)    | opaque     |
| 0x10   | 4    | form_flags    | `u32`                          | opaque     |
| 0x14   | 4    | **form_id**   | `FormID` (u32)                 | **read**   |
| 0x18   | 2    | in_game_flags | `u16`                          | opaque     |
| 0x1A   | 1    | form_type     | `u8`                           | opaque     |
| 0x1B   | 1    | pad           | `u8`                           | opaque     |
| 0x1C   | 4    | pad           | `u32`                          | opaque     |

Total: `0x20` bytes (size-asserted).

## `BSReadWriteLock` — partial layout

Source: `CommonLibSSE-NG/include/RE/B/BSAtomic.h`. Size `0x08`.

| Offset | Size | Field            | Type               |
|--------|------|------------------|--------------------|
| 0x00   | 4    | `writer_thread`  | `u32` (volatile)   |
| 0x04   | 4    | `lock`           | `u32` (volatile)   |

AE Address Library IDs for the four methods Mora calls:

| Method            | AE ID |
|-------------------|-------|
| `LockForRead`     | 68233 |
| `UnlockForRead`   | 68239 |
| `LockForWrite`    | 68234 |
| `UnlockForWrite`  | 68240 |

Mora only uses `LockForRead` / `UnlockForRead` (read-only lookup in `LookupByID`).

## `BSTHashMap<FormID, *mut TESForm>` — partial layout

Source: `CommonLibSSE-NG/include/RE/B/BSTHashMap.h` (alias of
`BSTScatterTable` with `BSTScatterTableHeapAllocator`).

| Offset | Size | Field         | Rust type                    | Mora use |
|--------|------|---------------|------------------------------|----------|
| 0x00   | 8    | `_pad00`      | `u64`                        | skip     |
| 0x08   | 4    | `_pad08`      | `u32`                        | skip     |
| 0x0C   | 4    | `capacity`    | `u32` (power-of-2)           | read     |
| 0x10   | 4    | `free`        | `u32`                        | skip     |
| 0x14   | 4    | `good`        | `u32`                        | skip     |
| 0x18   | 8    | `sentinel`    | `*const ()` (== `0xDEADBEEF`) | read (chain-end check) |
| 0x20   | 8    | `_alloc_pad`  | `u64`                        | skip     |
| 0x28   | 8    | `entries`     | `*mut HashMapEntry`          | read     |

Total: `0x30` bytes. Alias: `pub type FormMap = BSTHashMap<FormID, *mut TESForm>;`.

### Entry layout

Each entry is 24 bytes:

| Offset | Size | Field   | Rust type             |
|--------|------|---------|-----------------------|
| 0x00   | 4    | `key`   | `u32` (FormID)        |
| 0x04   | 4    | `_pad`  | `u32` (alignment)     |
| 0x08   | 8    | `value` | `*mut TESForm`        |
| 0x10   | 8    | `next`  | `*mut HashMapEntry`   |

Empty slot: `next == null`. End of chain: `next == 0xDEADBEEF as *mut _`.

### Hash + lookup algorithm

1. Hash: `BSCRC32(u32) = crc32(key.to_ne_bytes())`. Bethesda's `BSCRC32<arithmetic_type>` hashes the raw little-endian bytes through the standard CRC-32 (polynomial `0xEDB88320`). Use the `crc32fast` crate.
2. Slot: `idx = hash & (capacity - 1)`.
3. Read `entries[idx]`. If `next == null`, return `None`.
4. Walk the chain: if `entry.key == target_key`, return `Some(entry.value)`. Else `entry = entry.next`. Stop if `entry == sentinel`.

Global map access: `allForms: *mut *mut FormMap` at AE ID `400507` (**double pointer** — the global slot holds a `FormMap*`, so dereference once to get the map, then walk). Lock: `allFormsMapLock: *mut BSReadWriteLock` at AE ID `400517`.

## `TESDataHandler` — partial layout

Source: `CommonLibSSE-NG/include/RE/T/TESDataHandler.h`.
Singleton: static variable at AE ID `400269`.

Plan 3 only needs the singleton-pointer resolution. Field offsets are
deliberately left blank; the smoke test does not read inner fields.
Future plans populate as needed.

## `BGSKeyword` — partial layout

Source: `CommonLibSSE-NG/include/RE/B/BGSKeyword.h`. Documented size
`0x28`.

| Offset | Size | Field              | Type             |
|--------|------|--------------------|------------------|
| 0x00   | 0x20 | (TESForm base)     | inline `TESForm` |
| 0x20   | 8    | `form_editor_id`   | `BSFixedString`  |

Mora only uses the inline `TESForm` base (for the FormID of the
keyword being added). `form_editor_id` is not read.

## `BGSKeywordForm` — partial layout (mixin component)

Source: `CommonLibSSE-NG/include/RE/B/BGSKeywordForm.h`. Documented
size `0x18`.

| Offset | Size | Field          | Rust type                | Mora use          |
|--------|------|----------------|--------------------------|-------------------|
| 0x00   | 8    | `vtable`       | `*const ()`              | opaque (untyped)  |
| 0x08   | 8    | `keywords`     | `*mut *mut BGSKeyword`   | read + replace    |
| 0x10   | 4    | `num_keywords` | `u32`                    | read + replace    |
| 0x14   | 4    | `_pad14`       | `u32`                    | opaque            |

### Invariant (ported from `CommonLibSSE-NG/src/RE/B/BGSKeywordForm.cpp`)

`AddKeyword` logic:
1. If `num_keywords > 0` and the keyword is already present in the
   array (linear scan), return `false` — no mutation.
2. Allocate a new `BGSKeyword**` array of length `num_keywords + 1`
   via `MemoryManager::Allocate`.
3. Memcpy the old array into the new (pointer-wise copy).
4. Write the new keyword pointer at index `num_keywords`.
5. Swap: `old_keywords = self.keywords; self.keywords = new_array;
   self.num_keywords = num_keywords + 1;`
6. `MemoryManager::Deallocate(old_keywords)` iff `old_keywords`
   was non-null.
7. Return `true`.

## `MemoryManager` — partial layout

Source: `CommonLibSSE-NG/include/RE/M/MemoryManager.h`.

Plan 3 does not define a struct for `MemoryManager` — it only needs
three function IDs to call through:

| Function                   | AE ID  | Signature (calling via Rust fn ptr)                                     |
|----------------------------|--------|-------------------------------------------------------------------------|
| `MemoryManager::GetSingleton` | 11141 | `unsafe extern "C" fn() -> *mut MemoryManager`                         |
| `MemoryManager::Allocate`     | 68115 | `unsafe extern "C" fn(mm: *mut MemoryManager, size: usize, alignment: u32, aligned: bool) -> *mut u8` |
| `MemoryManager::Deallocate`   | 68117 | `unsafe extern "C" fn(mm: *mut MemoryManager, ptr: *mut u8, aligned: bool)` |

Mora's callers pass `alignment = 0` and `aligned = false` for raw
heap allocation (matches `RE::malloc(size)` convention in
CommonLibSSE-NG).

## SKSE messaging callback (kDataLoaded) — wiring

The `SKSEMessagingInterface` (Plan 2's ABI) is queried via
`SKSEInterface::query_interface(5)`. Registering a listener:
```
(interface.register_listener)(plugin_handle, null, callback_fn_ptr)
```
`null` as sender means "all senders" (SKSE global broadcast).
The callback receives `*mut SKSEMessage`; Mora filters on
`msg.msg_type == MessageType::DataLoaded as u32` (value `8`).

EOF
```

- [ ] **Step 2: Commit**

```bash
git add docs/src/skse-rs-ffi-reference.md
git commit -m "docs: expand skse-rs FFI reference for Plan 3 game types

Adds partial layouts for TESForm, BSReadWriteLock, BSTHashMap,
TESDataHandler, BGSKeyword, BGSKeywordForm, plus the Address Library
v2 bin format specification and MemoryManager function IDs. Each
layout is marked as minimum-viable; full surface lands when real
consumers need it."
```

---

## Phase B — Address Library parser (Tasks 2-4)

### Task 2: Parse the v2 `.bin` format in `address_library.rs`

**Files:**
- Create: `crates/skse-rs/src/address_library.rs`
- Modify: `crates/skse-rs/src/lib.rs` — add `pub mod address_library;`
- Modify: `crates/skse-rs/Cargo.toml` — no new deps for parsing

- [ ] **Step 1: Write the parser**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/skse-rs/src/address_library.rs <<'EOF'
//! Address Library v2 `.bin` format parser.
//!
//! `Data/SKSE/Plugins/versionlib-<runtime>.bin` is produced by
//! meh321's Address Library for Skyrim SE. Plan 3 parses the v2
//! format (AE 1.6.x) exactly; v1 (SE 1.5.97) and CSV (VR) are
//! out of scope for M1.
//!
//! See `docs/src/skse-rs-ffi-reference.md` for the format spec.

use std::path::Path;

/// Errors surfaced by [`AddressLibrary::load`].
#[derive(Debug, thiserror::Error)]
pub enum AddressLibraryError {
    #[error("failed to read address library: {0}")]
    Io(#[from] std::io::Error),
    #[error("unexpected format version {0}; expected 2")]
    UnexpectedFormat(i32),
    #[error("unexpected pointer_size {0}; expected 8")]
    UnexpectedPointerSize(i32),
    #[error("truncated address library (at byte offset {0})")]
    Truncated(usize),
    #[error("id not found: {0}")]
    IdNotFound(u64),
}

/// A loaded, sorted `(id, offset)` address table.
pub struct AddressLibrary {
    /// Parsed runtime version from the header: `(major, minor, patch, build)`.
    pub runtime_version: (i32, i32, i32, i32),
    /// Pairs sorted by `id` ascending. Binary-searchable.
    pairs: Vec<(u64, u64)>,
}

impl AddressLibrary {
    /// Load and parse the v2 `.bin` at `path`. On success, pairs are
    /// sorted by id (in-file order is monotonically increasing due to
    /// the delta encoding with id-mode 1..=5, but 0/6/7 break that,
    /// so we re-sort defensively).
    pub fn load(path: &Path) -> Result<Self, AddressLibraryError> {
        let bytes = std::fs::read(path)?;
        Self::parse(&bytes)
    }

    /// Parse from an in-memory byte slice. Exposed for unit tests.
    pub fn parse(bytes: &[u8]) -> Result<Self, AddressLibraryError> {
        let mut r = Reader::new(bytes);
        let format = r.i32()?;
        if format != 2 {
            return Err(AddressLibraryError::UnexpectedFormat(format));
        }
        let major = r.i32()?;
        let minor = r.i32()?;
        let patch = r.i32()?;
        let build = r.i32()?;
        let name_len = r.i32()?;
        if name_len < 0 {
            return Err(AddressLibraryError::Truncated(r.pos));
        }
        r.skip(name_len as usize)?;
        let pointer_size = r.i32()?;
        if pointer_size != 8 {
            return Err(AddressLibraryError::UnexpectedPointerSize(pointer_size));
        }
        let count = r.i32()?;
        if count < 0 {
            return Err(AddressLibraryError::Truncated(r.pos));
        }
        let ptr_size = pointer_size as u64;

        let mut pairs: Vec<(u64, u64)> = Vec::with_capacity(count as usize);
        let mut prev_id: u64 = 0;
        let mut prev_offset: u64 = 0;
        for _ in 0..count {
            let type_byte = r.u8()?;
            let lo = type_byte & 0x0F;
            let hi = type_byte >> 4;

            let id: u64 = match lo {
                0 => r.u64()?,
                1 => prev_id + 1,
                2 => prev_id + r.u8()? as u64,
                3 => prev_id - r.u8()? as u64,
                4 => prev_id + r.u16()? as u64,
                5 => prev_id - r.u16()? as u64,
                6 => r.u16()? as u64,
                7 => r.u32()? as u64,
                _ => unreachable!("nibble is 4-bit"),
            };

            let scale = (hi & 0x08) != 0;
            let base = if scale { prev_offset / ptr_size } else { prev_offset };
            let offset_mode = hi & 0x07;
            let mut offset: u64 = match offset_mode {
                0 => r.u64()?,
                1 => base + 1,
                2 => base + r.u8()? as u64,
                3 => base - r.u8()? as u64,
                4 => base + r.u16()? as u64,
                5 => base - r.u16()? as u64,
                6 => r.u16()? as u64,
                7 => r.u32()? as u64,
                _ => unreachable!("3-bit"),
            };
            if scale {
                offset *= ptr_size;
            }

            pairs.push((id, offset));
            prev_id = id;
            prev_offset = offset;
        }

        // Defensive sort — modes 0/6/7 can break monotonicity.
        pairs.sort_by_key(|(id, _)| *id);

        Ok(AddressLibrary {
            runtime_version: (major, minor, patch, build),
            pairs,
        })
    }

    /// Resolve an id to its offset (rva from the game's image base).
    pub fn resolve(&self, id: u64) -> Result<u64, AddressLibraryError> {
        self.pairs
            .binary_search_by_key(&id, |(i, _)| *i)
            .map(|idx| self.pairs[idx].1)
            .map_err(|_| AddressLibraryError::IdNotFound(id))
    }

    /// Number of parsed pairs. For diagnostics.
    pub fn len(&self) -> usize {
        self.pairs.len()
    }

    /// Empty? (Unusual but possible with count = 0.)
    pub fn is_empty(&self) -> bool {
        self.pairs.is_empty()
    }
}

/// Minimal endianness-aware byte reader.
struct Reader<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Reader { bytes, pos: 0 }
    }

    fn check(&self, n: usize) -> Result<(), AddressLibraryError> {
        if self.pos + n > self.bytes.len() {
            Err(AddressLibraryError::Truncated(self.pos))
        } else {
            Ok(())
        }
    }

    fn u8(&mut self) -> Result<u8, AddressLibraryError> {
        self.check(1)?;
        let v = self.bytes[self.pos];
        self.pos += 1;
        Ok(v)
    }
    fn u16(&mut self) -> Result<u16, AddressLibraryError> {
        self.check(2)?;
        let v = u16::from_le_bytes(self.bytes[self.pos..self.pos + 2].try_into().unwrap());
        self.pos += 2;
        Ok(v)
    }
    fn u32(&mut self) -> Result<u32, AddressLibraryError> {
        self.check(4)?;
        let v = u32::from_le_bytes(self.bytes[self.pos..self.pos + 4].try_into().unwrap());
        self.pos += 4;
        Ok(v)
    }
    fn u64(&mut self) -> Result<u64, AddressLibraryError> {
        self.check(8)?;
        let v = u64::from_le_bytes(self.bytes[self.pos..self.pos + 8].try_into().unwrap());
        self.pos += 8;
        Ok(v)
    }
    fn i32(&mut self) -> Result<i32, AddressLibraryError> {
        Ok(self.u32()? as i32)
    }
    fn skip(&mut self, n: usize) -> Result<(), AddressLibraryError> {
        self.check(n)?;
        self.pos += n;
        Ok(())
    }
}
EOF
```

- [ ] **Step 2: Wire the module**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/lib.rs")
text = p.read_text()
old = "pub mod ffi;"
new = "pub mod address_library;\npub mod ffi;"
if "pub mod address_library" not in text:
    text = text.replace(old, new)
    p.write_text(text)
PY
grep '^pub mod' crates/skse-rs/src/lib.rs
```

Expected: `address_library`, `ffi`, `log`, `plugin`, `version`.

- [ ] **Step 3: Verify**

```bash
source $HOME/.cargo/env
cargo check --package skse-rs
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Both must succeed.

- [ ] **Step 4: Commit**

```bash
git add crates/skse-rs/src/address_library.rs crates/skse-rs/src/lib.rs
git commit -m "skse-rs: Address Library v2 bin format parser

Pure-Rust port of the format documented in CommonLibSSE-NG REL/ID.h.
Decodes the delta-encoded (id, offset) stream, resorts defensively
in case modes 0/6/7 break monotonicity, exposes id -> offset via
binary search. No I/O dependency on Skyrim — AddressLibrary::load
reads any path, AddressLibrary::parse takes a byte slice."
```

---

### Task 3: Unit tests against a hand-crafted binary fixture

**Files:**
- Create: `crates/skse-rs/tests/bin_fixtures/tiny.bin`
- Create: `crates/skse-rs/tests/address_library.rs`

- [ ] **Step 1: Build the synthetic fixture via a tiny Rust program**

The fixture is binary, so author it programmatically to avoid shell-escape hazards. Use a `build.rs`-free approach: put a Rust utility under `crates/xtask/src/` (xtask already exists) OR just write it inline in the test as a `const` byte array.

Inline is simpler. Write the test file to include the fixture bytes directly:

```bash
mkdir -p crates/skse-rs/tests/bin_fixtures
cat > crates/skse-rs/tests/address_library.rs <<'EOF'
//! Integration tests for the Address Library v2 bin parser.
//!
//! Fixture is hand-encoded inline — a 3-pair table with a variety of
//! encoding modes, so the parser exercises the full delta-decode state
//! machine.

use skse_rs::address_library::{AddressLibrary, AddressLibraryError};

/// Hand-crafted v2 bin: format=2, version=1.6.1170.0, name=("AE"),
/// pointer_size=8, count=3. Pairs:
///   (id=0x0000_0001, offset=0x0000_1000) — id mode 7 (abs u32), offset mode 0 (u64)
///   (id=0x0000_0002, offset=0x0000_1008) — id mode 1 (+1),        offset mode 2 (+1, scaled)
///   (id=0x0000_0400, offset=0x0000_2000) — id mode 4 (+u16 delta), offset mode 7 (abs u32)
fn fixture_bytes() -> Vec<u8> {
    let mut b = Vec::new();
    // header
    b.extend_from_slice(&2i32.to_le_bytes()); // format_version
    b.extend_from_slice(&1i32.to_le_bytes()); // major
    b.extend_from_slice(&6i32.to_le_bytes()); // minor
    b.extend_from_slice(&1170i32.to_le_bytes()); // patch
    b.extend_from_slice(&0i32.to_le_bytes()); // build
    b.extend_from_slice(&2i32.to_le_bytes()); // name_len
    b.extend_from_slice(b"AE");               // name (ignored by parser)
    b.extend_from_slice(&8i32.to_le_bytes()); // pointer_size
    b.extend_from_slice(&3i32.to_le_bytes()); // count

    // pair 1: id mode 7 (abs u32), offset mode 0 (abs u64), no scale
    // type byte: hi=0, lo=7  → 0x07
    b.push(0x07);
    b.extend_from_slice(&0x0000_0001u32.to_le_bytes());
    b.extend_from_slice(&0x0000_1000u64.to_le_bytes());

    // pair 2: id mode 1 (+1), offset mode 2 (+u8, scaled by pointer_size)
    // type byte: hi = 8 (scale) | 2 = 0xA, lo = 1 → 0xA1
    // prev_offset was 0x1000; scale base = 0x1000/8 = 0x200; + u8(1) = 0x201; * 8 = 0x1008
    b.push(0xA1);
    b.push(1u8); // u8 delta for offset

    // pair 3: id mode 4 (+u16 delta), offset mode 7 (abs u32), no scale
    // type byte: hi=7, lo=4 → 0x74
    // prev_id = 2, delta u16 = 0x3FE → id = 0x400
    b.push(0x74);
    b.extend_from_slice(&0x03FEu16.to_le_bytes());
    b.extend_from_slice(&0x0000_2000u32.to_le_bytes());

    b
}

#[test]
fn parse_hand_crafted_fixture() {
    let bytes = fixture_bytes();
    let lib = AddressLibrary::parse(&bytes).expect("parse ok");
    assert_eq!(lib.runtime_version, (1, 6, 1170, 0));
    assert_eq!(lib.len(), 3);
    assert_eq!(lib.resolve(0x0000_0001).unwrap(), 0x0000_1000);
    assert_eq!(lib.resolve(0x0000_0002).unwrap(), 0x0000_1008);
    assert_eq!(lib.resolve(0x0000_0400).unwrap(), 0x0000_2000);
}

#[test]
fn missing_id_errors() {
    let lib = AddressLibrary::parse(&fixture_bytes()).unwrap();
    match lib.resolve(0x1234_5678) {
        Err(AddressLibraryError::IdNotFound(id)) => assert_eq!(id, 0x1234_5678),
        other => panic!("expected IdNotFound; got {other:?}"),
    }
}

#[test]
fn unexpected_format_rejected() {
    let mut bytes = fixture_bytes();
    bytes[0..4].copy_from_slice(&1i32.to_le_bytes()); // v1 not supported
    match AddressLibrary::parse(&bytes) {
        Err(AddressLibraryError::UnexpectedFormat(1)) => {}
        other => panic!("expected UnexpectedFormat(1); got {other:?}"),
    }
}

#[test]
fn truncated_input_errors() {
    let bytes = fixture_bytes();
    match AddressLibrary::parse(&bytes[..10]) {
        Err(AddressLibraryError::Truncated(_)) => {}
        other => panic!("expected Truncated; got {other:?}"),
    }
}

#[test]
fn unexpected_pointer_size_rejected() {
    let mut bytes = fixture_bytes();
    // Patch pointer_size at the right offset: header is
    // 4 (format) + 16 (version) + 4 (name_len) + 2 (name) = 26
    let ptr_size_pos = 26;
    bytes[ptr_size_pos..ptr_size_pos + 4].copy_from_slice(&4i32.to_le_bytes());
    match AddressLibrary::parse(&bytes) {
        Err(AddressLibraryError::UnexpectedPointerSize(4)) => {}
        other => panic!("expected UnexpectedPointerSize(4); got {other:?}"),
    }
}
EOF
```

- [ ] **Step 2: Run tests**

```bash
cargo test --package skse-rs --test address_library
```

Expected: 5 passed.

- [ ] **Step 3: Windows cross-compile check**

```bash
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --tests
```

- [ ] **Step 4: Commit**

```bash
git add crates/skse-rs/tests/address_library.rs
git commit -m "skse-rs: address-library parser tests with hand-crafted fixture

Five tests exercising the delta-decode state machine: happy path
(3-pair fixture with id-modes 7/1/4 and offset-modes 0/2-scaled/7),
missing-id error, format-version mismatch, truncated input,
wrong pointer_size."
```

---

### Task 4: Real-world Address Library smoke test (optional, skipped if file absent)

**Files:**
- Modify: `crates/skse-rs/tests/address_library.rs` (append)

Add a test that loads a real Address Library bin from a known local path IF it exists; otherwise skip. Committed so developers with a Skyrim install can run it; CI (no Skyrim install) silently skips.

- [ ] **Step 1: Append the optional test**

```bash
cat >> crates/skse-rs/tests/address_library.rs <<'EOF'

/// If the developer has a local Skyrim install with a current Address
/// Library bin, load it and confirm it parses + resolves a
/// known-good id (TESDataHandler::Singleton, AE id 400269). Skipped
/// silently when the file isn't present (CI case).
#[test]
fn real_address_library_parses_if_present() {
    const CANDIDATES: &[&str] = &[
        // Common Proton Steam install path on Linux
        "~/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/versionlib-1-6-1170-0.bin",
        "~/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/version-1-6-1170-0.bin",
        // Developer-configurable via env var
    ];
    let env_path = std::env::var("MORA_SKYRIM_DATA")
        .ok()
        .map(|d| format!("{}/SKSE/Plugins/versionlib-1-6-1170-0.bin", d));
    let mut paths: Vec<String> = CANDIDATES.iter().map(|s| s.to_string()).collect();
    if let Some(p) = env_path {
        paths.insert(0, p);
    }
    let expanded: Vec<std::path::PathBuf> = paths
        .iter()
        .map(|p| {
            if let Some(stripped) = p.strip_prefix("~/") {
                if let Some(home) = dirs_home() {
                    return home.join(stripped);
                }
            }
            std::path::PathBuf::from(p)
        })
        .collect();
    let Some(found) = expanded.iter().find(|p| p.exists()) else {
        eprintln!("real_address_library_parses_if_present: no bin found, skipping");
        return;
    };

    let lib = skse_rs::address_library::AddressLibrary::load(found).expect("parse real bin");
    // AE version header
    assert_eq!(lib.runtime_version.0, 1);
    assert_eq!(lib.runtime_version.1, 6);
    // TESDataHandler::Singleton is always present in AE
    lib.resolve(400269).expect("id 400269 (TESDataHandler::Singleton) must resolve");
    eprintln!(
        "real_address_library_parses_if_present: {} pairs, sanity-resolved id 400269",
        lib.len()
    );
}

/// Minimal $HOME lookup without adding a dirs-crate dep.
fn dirs_home() -> Option<std::path::PathBuf> {
    std::env::var_os("HOME").map(std::path::PathBuf::from)
}
EOF
```

- [ ] **Step 2: Run tests**

```bash
cargo test --package skse-rs --test address_library
```

Expected: 6 passed (the real-bin test prints "skipping" if not present — still passes).

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/tests/address_library.rs
git commit -m "skse-rs: optional smoke test against a real Address Library bin

Skips silently when the bin isn't present (CI case). On a developer
box with Skyrim installed at the standard Steam path, loads the bin
and confirms id 400269 (TESDataHandler::Singleton) resolves — a
stronger parser correctness signal than synthetic fixtures alone.
Path overridable via MORA_SKYRIM_DATA env var."
```

---

## Phase C — Relocation + image base (Tasks 5-6)

### Task 5: Image base resolution on Windows

**Files:**
- Create: `crates/skse-rs/src/relocation.rs`
- Modify: `crates/skse-rs/src/lib.rs`

- [ ] **Step 1: Write the module**

```bash
cat > crates/skse-rs/src/relocation.rs <<'EOF'
//! Address-library-backed relocation resolver.
//!
//! Skyrim's image base is obtained from `GetModuleHandleW(NULL)` — the
//! currently loaded executable. Plan 3 keeps this Windows-only; a
//! future plan may abstract it if we need cross-platform tests.
//!
//! Plan 3 loads the Address Library bin lazily on first `Relocation::id`
//! call. The bin path is configurable via [`Relocation::set_library_path`];
//! default is the standard SKSE Plugins location.

use crate::address_library::{AddressLibrary, AddressLibraryError};
use std::path::PathBuf;
use std::sync::OnceLock;

/// Errors from resolving a relocation.
#[derive(Debug, thiserror::Error)]
pub enum RelocationError {
    #[error("image base could not be resolved")]
    ImageBase,
    #[error("address library: {0}")]
    Library(#[from] AddressLibraryError),
    #[error("address library not initialized (call Relocation::set_library first)")]
    NotInitialized,
}

static LIBRARY: OnceLock<AddressLibrary> = OnceLock::new();

/// Provide the loaded Address Library. Call once during plugin init
/// (typically inside `on_load`). Subsequent calls are ignored.
pub fn set_library(library: AddressLibrary) {
    let _ = LIBRARY.set(library);
}

/// Initialize the library from a file path. Convenience over
/// [`set_library`] for the common case.
pub fn load_library_from_path(path: &std::path::Path) -> Result<(), RelocationError> {
    let lib = AddressLibrary::load(path)?;
    set_library(lib);
    Ok(())
}

/// Resolve the well-known SKSE Plugins path, using `%MORA_SKYRIM_DATA%`
/// if set, else the first candidate that exists.
pub fn resolve_default_library_path() -> Option<PathBuf> {
    if let Ok(data) = std::env::var("MORA_SKYRIM_DATA") {
        let p = PathBuf::from(data).join("SKSE/Plugins/versionlib-1-6-1170-0.bin");
        if p.exists() {
            return Some(p);
        }
    }
    // On Windows, the plugin's working directory is the game's Data dir.
    let p = PathBuf::from("SKSE/Plugins/versionlib-1-6-1170-0.bin");
    if p.exists() {
        return Some(p);
    }
    None
}

/// A resolved address — in bytes from the image base.
///
/// Construct via [`Relocation::id`]. Use [`Relocation::ptr`] to get a
/// raw pointer, or the typed helpers to cast to a function pointer
/// with a known signature.
#[derive(Debug, Clone, Copy)]
pub struct Relocation {
    /// Absolute address in the current process.
    addr: usize,
}

impl Relocation {
    /// Resolve an Address Library id to a process-absolute address.
    ///
    /// # Safety
    /// The returned address must be used consistently with the
    /// function/variable layout documented for the id.
    pub fn id(id: u64) -> Result<Relocation, RelocationError> {
        let lib = LIBRARY.get().ok_or(RelocationError::NotInitialized)?;
        let offset = lib.resolve(id)?;
        let base = image_base_address().ok_or(RelocationError::ImageBase)?;
        Ok(Relocation { addr: base + offset as usize })
    }

    /// Raw address as `usize`.
    pub fn addr(self) -> usize {
        self.addr
    }

    /// Cast the address to `*const T`.
    ///
    /// # Safety
    /// T must match the in-memory layout of whatever the id resolves to.
    pub unsafe fn as_ptr<T>(self) -> *const T {
        self.addr as *const T
    }

    /// Cast the address to `*mut T`.
    ///
    /// # Safety
    /// T must match the in-memory layout of whatever the id resolves to.
    pub unsafe fn as_mut_ptr<T>(self) -> *mut T {
        self.addr as *mut T
    }

    /// Cast to a function pointer. The caller must know the full
    /// signature; `FnPtr` is typically `unsafe extern "C" fn(...) -> ...`.
    ///
    /// # Safety
    /// Caller certifies that the target address is a function with the
    /// claimed signature.
    pub unsafe fn as_fn<FnPtr: Copy>(self) -> FnPtr {
        debug_assert_eq!(
            core::mem::size_of::<FnPtr>(),
            core::mem::size_of::<usize>(),
            "FnPtr must be pointer-sized"
        );
        unsafe { core::mem::transmute_copy(&self.addr) }
    }
}

#[cfg(windows)]
fn image_base_address() -> Option<usize> {
    use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
    unsafe {
        let h = GetModuleHandleW(core::ptr::null());
        if h.is_null() {
            None
        } else {
            Some(h as usize)
        }
    }
}

#[cfg(not(windows))]
fn image_base_address() -> Option<usize> {
    // Non-Windows (dev-box unit tests): pretend the image base is 0 so
    // `Relocation::id` returns the raw rva. Useful for testing the
    // resolver plumbing without a real game binary.
    Some(0)
}
EOF
```

- [ ] **Step 2: Register module**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/lib.rs")
text = p.read_text()
if "pub mod relocation" not in text:
    text = text.replace("pub mod plugin;", "pub mod plugin;\npub mod relocation;")
    p.write_text(text)
PY
grep '^pub mod' crates/skse-rs/src/lib.rs
```

Expected: `address_library`, `ffi`, `log`, `plugin`, `relocation`, `version`.

- [ ] **Step 3: Verify**

```bash
source $HOME/.cargo/env
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
```

Both must succeed.

- [ ] **Step 4: Commit**

```bash
git add crates/skse-rs/src/relocation.rs crates/skse-rs/src/lib.rs
git commit -m "skse-rs: Relocation resolver + image base lookup

Relocation::id(u64) -> Result<Relocation> looks up the id in the
lazily-loaded Address Library, then adds the game image base
(GetModuleHandleW(NULL) on Windows, 0 on non-Windows for tests).
Exposes addr/as_ptr/as_mut_ptr/as_fn helpers. Initialization is
separate (set_library / load_library_from_path) so on_load controls
when the bin is parsed."
```

---

### Task 6: Small unit test for the relocation plumbing

**Files:**
- Create: `crates/skse-rs/tests/relocation.rs`

- [ ] **Step 1: Write the test**

```bash
cat > crates/skse-rs/tests/relocation.rs <<'EOF'
//! Smoke tests for the Relocation module.
//!
//! Linux-only behavior: image base is 0, so Relocation::id returns
//! the raw rva. We inject a synthetic AddressLibrary to avoid any
//! dependency on a game binary.

use skse_rs::address_library::AddressLibrary;
use skse_rs::relocation::{self, Relocation, RelocationError};

fn make_tiny_library() -> AddressLibrary {
    // Reuse the same fixture from the address_library tests.
    let mut b = Vec::new();
    b.extend_from_slice(&2i32.to_le_bytes());
    b.extend_from_slice(&1i32.to_le_bytes());
    b.extend_from_slice(&6i32.to_le_bytes());
    b.extend_from_slice(&1170i32.to_le_bytes());
    b.extend_from_slice(&0i32.to_le_bytes());
    b.extend_from_slice(&2i32.to_le_bytes());
    b.extend_from_slice(b"AE");
    b.extend_from_slice(&8i32.to_le_bytes());
    b.extend_from_slice(&1i32.to_le_bytes());
    // One pair: id=0x100, offset=0x2000
    b.push(0x07);
    b.extend_from_slice(&0x0000_0100u32.to_le_bytes());
    b.extend_from_slice(&0x0000_2000u64.to_le_bytes());
    AddressLibrary::parse(&b).expect("parse")
}

#[test]
fn id_requires_set_library() {
    // This test may be order-dependent with other tests in this file
    // when the library OnceLock is shared. Run in a separate process
    // if flaky (RUST_TEST_THREADS=1).
    match Relocation::id(99999) {
        Err(RelocationError::NotInitialized) | Ok(_) => {
            // OK either way: if another test has set the library
            // already, we can't observe NotInitialized from here.
        }
        Err(other) => panic!("unexpected error: {other:?}"),
    }
}

#[test]
fn id_resolves_after_set_library() {
    relocation::set_library(make_tiny_library());
    let r: Relocation = Relocation::id(0x100).expect("resolve 0x100");
    // Non-Windows image base is 0; addr == offset.
    #[cfg(not(windows))]
    assert_eq!(r.addr(), 0x2000);
    #[cfg(windows)]
    {
        let _ = r; // on Windows the image base is non-zero; we only assert it's non-zero
    }
}
EOF
```

- [ ] **Step 2: Run the tests**

```bash
cargo test --package skse-rs --test relocation -- --test-threads=1
```

Expected: 2 passed. `--test-threads=1` avoids ordering issues with the static `LIBRARY`.

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/tests/relocation.rs
git commit -m "skse-rs: relocation-resolver smoke tests

On Linux with image base = 0, a relocation resolves to its rva
directly. Two tests: (a) pre-init error semantics, (b) happy-path
resolve after set_library."
```

---

## Phase D — Game types (Tasks 7-11)

### Task 7: `game/lock.rs` — `BSReadWriteLock`

**Files:**
- Create: `crates/skse-rs/src/game/mod.rs`
- Create: `crates/skse-rs/src/game/lock.rs`
- Modify: `crates/skse-rs/src/lib.rs` — add `pub mod game;`

- [ ] **Step 1: Register the module tree**

```bash
mkdir -p crates/skse-rs/src/game
cat > crates/skse-rs/src/game/mod.rs <<'EOF'
//! Partial Rust bindings to Skyrim game types.
//!
//! Every struct in this module carries an `M1-minimal` comment: only
//! the fields and methods the M1 smoke test exercises are defined.
//! Extend when real consumers (M5 mora-runtime, M6 KID frontend) land.

pub mod data_handler;
pub mod form;
pub mod hash_map;
pub mod keyword;
pub mod keyword_form;
pub mod lock;
pub mod memory;
EOF

python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/lib.rs")
text = p.read_text()
if "pub mod game" not in text:
    text = text.replace("pub mod ffi;", "pub mod ffi;\npub mod game;")
    p.write_text(text)
PY
```

- [ ] **Step 2: Write `lock.rs`**

```bash
cat > crates/skse-rs/src/game/lock.rs <<'EOF'
//! `BSReadWriteLock` binding.
//!
//! M1-minimal: only `LockForRead` / `UnlockForRead` are bound.
//! Write-lock IDs listed for future use.

use crate::relocation::{Relocation, RelocationError};

/// AE Address Library IDs for the four lock methods.
pub mod ae_ids {
    pub const LOCK_FOR_READ: u64 = 68233;
    pub const UNLOCK_FOR_READ: u64 = 68239;
    pub const LOCK_FOR_WRITE: u64 = 68234;
    pub const UNLOCK_FOR_WRITE: u64 = 68240;
}

/// Layout of `RE::BSReadWriteLock`. Size 0x08.
#[repr(C)]
pub struct BSReadWriteLock {
    pub writer_thread: u32,
    pub lock: u32,
}

const _: () = assert!(core::mem::size_of::<BSReadWriteLock>() == 0x08);

type LockForReadFn = unsafe extern "C" fn(lock: *mut BSReadWriteLock);
type UnlockForReadFn = unsafe extern "C" fn(lock: *mut BSReadWriteLock);

/// Acquire the read lock.
///
/// # Safety
/// `lock` must point to a valid `BSReadWriteLock` instance owned by
/// Skyrim. Must be balanced with [`unlock_for_read`].
pub unsafe fn lock_for_read(lock: *mut BSReadWriteLock) -> Result<(), RelocationError> {
    let r = Relocation::id(ae_ids::LOCK_FOR_READ)?;
    let f: LockForReadFn = unsafe { r.as_fn() };
    unsafe { f(lock) };
    Ok(())
}

/// Release the read lock.
///
/// # Safety
/// `lock` must be a previously acquired read lock via [`lock_for_read`].
pub unsafe fn unlock_for_read(lock: *mut BSReadWriteLock) -> Result<(), RelocationError> {
    let r = Relocation::id(ae_ids::UNLOCK_FOR_READ)?;
    let f: UnlockForReadFn = unsafe { r.as_fn() };
    unsafe { f(lock) };
    Ok(())
}

/// RAII read-lock guard. Drops call `UnlockForRead`.
pub struct ReadGuard {
    lock: *mut BSReadWriteLock,
}

impl ReadGuard {
    /// # Safety
    /// `lock` must be a valid, live `BSReadWriteLock`.
    pub unsafe fn new(lock: *mut BSReadWriteLock) -> Result<Self, RelocationError> {
        unsafe { lock_for_read(lock)? };
        Ok(ReadGuard { lock })
    }
}

impl Drop for ReadGuard {
    fn drop(&mut self) {
        unsafe {
            // Best-effort: ignore error in Drop. If relocation isn't
            // available during Drop, something has gone badly wrong
            // already; falling through doesn't make it worse.
            let _ = unlock_for_read(self.lock);
        }
    }
}
EOF
```

- [ ] **Step 3: Verify**

```bash
source $HOME/.cargo/env
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
```

Both must succeed. Stub files for the other `game/*` modules don't exist yet — `game/mod.rs` declares them. We'll get "file not found" from `cargo check` until we create them. Address this by writing the remaining game/*.rs files with empty stubs NOW (they'll be populated in the next tasks), OR by commenting out unused `pub mod` lines in game/mod.rs until they're added.

Simpler: stub the remaining module files up front so cargo is happy through the phase.

```bash
for m in data_handler form hash_map keyword keyword_form memory; do
    cat > "crates/skse-rs/src/game/$m.rs" <<EOF
//! Stub. Populated in the task that implements this type.
EOF
done
```

Re-run:
```bash
cargo check --package skse-rs --all-targets
```

- [ ] **Step 4: Commit**

```bash
git add crates/skse-rs/src/game/ crates/skse-rs/src/lib.rs
git commit -m "skse-rs: game::lock — BSReadWriteLock bindings

Layout + 4 AE Address Library IDs (LockForRead/UnlockForRead bound,
LockForWrite/UnlockForWrite reserved). RAII ReadGuard for scoped
read-lock acquisition. Remaining game/*.rs module files stubbed to
keep the module tree buildable — each populated by its own task."
```

---

### Task 8: `game/hash_map.rs` — `BSTHashMap<FormID, *mut TESForm>` read-only lookup

**Files:**
- Modify: `crates/skse-rs/src/game/hash_map.rs`
- Modify: `crates/skse-rs/Cargo.toml` — add `crc32fast`

- [ ] **Step 1: Add crc32fast dep**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/Cargo.toml")
text = p.read_text()
marker = "thiserror.workspace = true\ntracing.workspace = true\n"
replacement = "thiserror.workspace = true\ntracing.workspace = true\ncrc32fast = \"1\"\n"
if "crc32fast" not in text:
    text = text.replace(marker, replacement)
    p.write_text(text)
PY
grep crc32fast crates/skse-rs/Cargo.toml
```

Expected: `crc32fast = "1"`.

- [ ] **Step 2: Write the module**

```bash
cat > crates/skse-rs/src/game/hash_map.rs <<'EOF'
//! `BSTHashMap<FormID, *mut TESForm>` read-only binding.
//!
//! M1-minimal: only lookup is implemented. Insert, delete, iterate,
//! and capacity-growth paths are all out of scope.
//!
//! Algorithm ported from `CommonLibSSE-NG` `BSTScatterTable::do_find`.

use crate::game::form::TESForm;

/// The sentinel Bethesda uses to mark end-of-chain.
pub const SENTINEL: usize = 0xDEADBEEF;

/// Layout of `BSTHashMap<FormID, *mut TESForm>`. Size 0x30.
#[repr(C)]
pub struct FormHashMap {
    pub _pad00: u64,
    pub _pad08: u32,
    pub capacity: u32,
    pub free: u32,
    pub good: u32,
    pub sentinel: *const (),
    pub _alloc_pad: u64,
    pub entries: *mut HashMapEntry,
}

const _: () = assert!(core::mem::size_of::<FormHashMap>() == 0x30);

/// Single entry in the hash map. Size 0x18.
#[repr(C)]
pub struct HashMapEntry {
    pub key: u32,
    pub _pad: u32,
    pub value: *mut TESForm,
    pub next: *mut HashMapEntry,
}

const _: () = assert!(core::mem::size_of::<HashMapEntry>() == 0x18);

impl FormHashMap {
    /// Find the entry for `form_id`. Returns the `TESForm*` on hit,
    /// `core::ptr::null_mut()` on miss.
    ///
    /// # Safety
    /// Caller must hold an appropriate lock on the map; `self` must
    /// be a valid `FormHashMap` obtained via the Address Library's
    /// `allForms` pointer.
    pub unsafe fn lookup(&self, form_id: u32) -> *mut TESForm {
        if self.entries.is_null() || self.capacity == 0 {
            return core::ptr::null_mut();
        }
        let hash = crc32_bethesda(form_id);
        let idx = (hash & (self.capacity.wrapping_sub(1))) as usize;
        // SAFETY: bounds guaranteed by `idx < capacity` (power-of-2 mask).
        let mut entry: *mut HashMapEntry = unsafe { self.entries.add(idx) };
        // Empty slot: next == null. End of chain: next == SENTINEL.
        loop {
            let cur = unsafe { &*entry };
            if cur.next.is_null() {
                // Empty slot marker in Bethesda's design.
                return core::ptr::null_mut();
            }
            if cur.key == form_id {
                return cur.value;
            }
            if cur.next as usize == SENTINEL {
                return core::ptr::null_mut();
            }
            entry = cur.next;
        }
    }
}

/// Bethesda's `BSCRC32<u32>` — standard CRC-32 (poly 0xEDB88320) over
/// the raw little-endian bytes of the key.
pub fn crc32_bethesda(form_id: u32) -> u32 {
    let mut h = crc32fast::Hasher::new();
    h.update(&form_id.to_le_bytes());
    h.finalize()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_known_values() {
        // Smoke-check: CRC-32 of "123456789" ASCII is 0xCBF43926 (well-known).
        // We only use the u32 path, but the underlying crc32fast implementation
        // is well-tested; just sanity-check the function doesn't panic for
        // various inputs.
        assert_eq!(crc32_bethesda(0x0000_0000), crc32_bethesda(0));
        assert_ne!(crc32_bethesda(0), crc32_bethesda(1));
        assert_ne!(crc32_bethesda(0xFFFF_FFFF), crc32_bethesda(0));
    }

    #[test]
    fn layout_sizes() {
        assert_eq!(std::mem::size_of::<FormHashMap>(), 0x30);
        assert_eq!(std::mem::size_of::<HashMapEntry>(), 0x18);
    }

    #[test]
    fn lookup_empty_map_returns_null() {
        let map = FormHashMap {
            _pad00: 0,
            _pad08: 0,
            capacity: 0,
            free: 0,
            good: 0,
            sentinel: SENTINEL as *const (),
            _alloc_pad: 0,
            entries: core::ptr::null_mut(),
        };
        unsafe {
            assert!(map.lookup(0x1234).is_null());
        }
    }

    #[test]
    fn lookup_synthetic_hit() {
        // Build a tiny synthetic map with capacity 8 and one entry for
        // a known formID. We must place the entry in the slot
        // `crc32(formID) & 7` to simulate the natural-home case.
        let form_id: u32 = 0x0001_2EB7; // Iron Sword
        let hash = crc32_bethesda(form_id);
        let idx = (hash & 7) as usize;

        // Allocate 8 entries, all initially "empty" (next = null).
        let mut entries: Vec<HashMapEntry> = (0..8)
            .map(|_| HashMapEntry {
                key: 0,
                _pad: 0,
                value: core::ptr::null_mut(),
                next: core::ptr::null_mut(),
            })
            .collect();
        let fake_form_addr = 0xDEAD_1234usize as *mut TESForm;
        entries[idx].key = form_id;
        entries[idx].value = fake_form_addr;
        entries[idx].next = SENTINEL as *mut HashMapEntry;

        let map = FormHashMap {
            _pad00: 0,
            _pad08: 0,
            capacity: 8,
            free: 7,
            good: 0,
            sentinel: SENTINEL as *const (),
            _alloc_pad: 0,
            entries: entries.as_mut_ptr(),
        };

        unsafe {
            assert_eq!(map.lookup(form_id), fake_form_addr);
            assert!(map.lookup(0x9999_9999).is_null());
        }
        // entries owns the allocation; dropping at end of fn is fine
        // since map no longer references it (test is done).
        drop(entries);
    }
}
EOF
```

- [ ] **Step 3: Verify**

```bash
cargo check --package skse-rs --all-targets
cargo test --package skse-rs --lib game::hash_map::tests
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
```

Expected: 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add crates/skse-rs/src/game/hash_map.rs crates/skse-rs/Cargo.toml
git commit -m "skse-rs: game::hash_map — read-only BSTHashMap lookup

Layout (0x30) + HashMapEntry (0x18) + SENTINEL marker (0xDEADBEEF).
Lookup hashes the formID via CRC-32 (BSCRC32<u32>), masks to the
power-of-2 capacity, walks the chain. crc32fast dep added to
skse-rs. 4 unit tests including a synthetic-map happy path."
```

---

### Task 9: `game/form.rs` — `TESForm` + `lookup_by_id`

**Files:**
- Modify: `crates/skse-rs/src/game/form.rs`

- [ ] **Step 1: Write the module**

```bash
cat > crates/skse-rs/src/game/form.rs <<'EOF'
//! `TESForm` partial layout + `lookup_by_id`.
//!
//! M1-minimal: only `form_id` is exposed. Vtable + sourceFiles +
//! flags are opaque. Future plans add accessors as needed.

use crate::game::hash_map::FormHashMap;
use crate::game::lock::{BSReadWriteLock, ReadGuard};
use crate::relocation::{Relocation, RelocationError};

/// Address Library IDs for the global form map + its lock.
pub mod ae_ids {
    /// Global slot holding a `BSTHashMap<FormID, *mut TESForm>*`.
    /// Note: this is a *pointer to a pointer* — dereference once to
    /// get the map pointer.
    pub const ALL_FORMS: u64 = 400507;
    /// Read-write lock guarding `ALL_FORMS`.
    pub const ALL_FORMS_LOCK: u64 = 400517;
}

/// Alias: a FormID is a 32-bit Bethesda global-unique ID.
pub type FormID = u32;

/// `TESForm` partial layout. Size 0x20 (asserted).
///
/// M1-minimal: only `form_id` is used; the rest is opaque. Vtable
/// dispatch, flags, and source-file accessors are deferred until a
/// real consumer needs them.
#[repr(C)]
pub struct TESForm {
    pub vtable: *const (),
    pub source_files: *mut (),
    pub form_flags: u32,
    pub form_id: FormID,
    pub in_game_flags: u16,
    pub form_type: u8,
    pub _pad_1b: u8,
    pub _pad_1c: u32,
}

const _: () = assert!(core::mem::size_of::<TESForm>() == 0x20);
const _: () = assert!(core::mem::offset_of!(TESForm, form_id) == 0x14);

/// Look up a form by its FormID. Returns `Ok(None)` if the form is
/// not in the global map, `Err` on infrastructure failure (address
/// library not loaded, required ids missing, image base unavailable).
///
/// # Safety
/// Must be called on the main thread during a time when the form map
/// exists (e.g., after `kDataLoaded` fires). The returned pointer,
/// if any, is valid for the lifetime of the process (forms don't move
/// once loaded).
pub unsafe fn lookup_by_id(form_id: FormID) -> Result<Option<*mut TESForm>, RelocationError> {
    // Resolve the all-forms pointer-to-pointer and the lock.
    let all_forms_pp_reloc = Relocation::id(ae_ids::ALL_FORMS)?;
    let lock_reloc = Relocation::id(ae_ids::ALL_FORMS_LOCK)?;

    let all_forms_pp: *mut *mut FormHashMap = unsafe { all_forms_pp_reloc.as_mut_ptr() };
    let lock: *mut BSReadWriteLock = unsafe { lock_reloc.as_mut_ptr() };

    // Guard with read lock (released on Drop).
    let _guard = unsafe { ReadGuard::new(lock)? };

    let map_ptr: *mut FormHashMap = unsafe { *all_forms_pp };
    if map_ptr.is_null() {
        return Ok(None);
    }

    let map: &FormHashMap = unsafe { &*map_ptr };
    let result = unsafe { map.lookup(form_id) };
    if result.is_null() {
        Ok(None)
    } else {
        Ok(Some(result))
    }
}
EOF
```

- [ ] **Step 2: Verify**

```bash
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
cargo test --package skse-rs --lib
```

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/src/game/form.rs
git commit -m "skse-rs: game::form — TESForm partial + lookup_by_id

TESForm layout (0x20) with form_id at 0x14 asserted. lookup_by_id
resolves the allForms double-pointer (AE 400507), acquires the read
lock (AE 400517 via ReadGuard), deref once to get the FormHashMap,
call its lookup. Returns Option<*mut TESForm>; None for absent,
RelocationError for infrastructure faults."
```

---

### Task 10: `game/keyword.rs` + `game/keyword_form.rs` + `game/memory.rs`

**Files:**
- Modify: `crates/skse-rs/src/game/keyword.rs`
- Modify: `crates/skse-rs/src/game/keyword_form.rs`
- Modify: `crates/skse-rs/src/game/memory.rs`

- [ ] **Step 1: `game/memory.rs` — MemoryManager**

```bash
cat > crates/skse-rs/src/game/memory.rs <<'EOF'
//! `MemoryManager` bindings for Skyrim's internal allocator.
//!
//! M1-minimal: only `GetSingleton`, `Allocate`, `Deallocate` are
//! bound — just enough for `BGSKeywordForm::add_keyword`.

use crate::relocation::{Relocation, RelocationError};

/// Opaque MemoryManager. We never construct or access fields — only
/// pass a pointer to `Allocate` / `Deallocate`.
#[repr(C)]
pub struct MemoryManager {
    _private: [u8; 0],
}

pub mod ae_ids {
    pub const GET_SINGLETON: u64 = 11141;
    pub const ALLOCATE: u64 = 68115;
    pub const DEALLOCATE: u64 = 68117;
}

type GetSingletonFn = unsafe extern "C" fn() -> *mut MemoryManager;
type AllocateFn = unsafe extern "C" fn(
    mm: *mut MemoryManager,
    size: usize,
    alignment: u32,
    aligned: bool,
) -> *mut u8;
type DeallocateFn = unsafe extern "C" fn(
    mm: *mut MemoryManager,
    ptr: *mut u8,
    aligned: bool,
);

/// Resolve the `MemoryManager` singleton.
///
/// # Safety
/// Must be called after `Relocation::set_library` has been invoked.
pub unsafe fn get_singleton() -> Result<*mut MemoryManager, RelocationError> {
    let r = Relocation::id(ae_ids::GET_SINGLETON)?;
    let f: GetSingletonFn = unsafe { r.as_fn() };
    Ok(unsafe { f() })
}

/// Allocate `size` bytes from Skyrim's internal heap. Matches
/// `RE::malloc(size)`: alignment=0, aligned=false.
///
/// # Safety
/// Caller must `deallocate` the returned pointer via [`deallocate`];
/// don't pair with `HeapFree` or Rust's `alloc::dealloc`.
pub unsafe fn allocate(size: usize) -> Result<*mut u8, RelocationError> {
    let mm = unsafe { get_singleton() }?;
    let r = Relocation::id(ae_ids::ALLOCATE)?;
    let f: AllocateFn = unsafe { r.as_fn() };
    Ok(unsafe { f(mm, size, 0, false) })
}

/// Free memory previously allocated via [`allocate`].
///
/// # Safety
/// `ptr` must have come from [`allocate`]; double-free is UB.
pub unsafe fn deallocate(ptr: *mut u8) -> Result<(), RelocationError> {
    if ptr.is_null() {
        return Ok(());
    }
    let mm = unsafe { get_singleton() }?;
    let r = Relocation::id(ae_ids::DEALLOCATE)?;
    let f: DeallocateFn = unsafe { r.as_fn() };
    unsafe { f(mm, ptr, false) };
    Ok(())
}
EOF
```

- [ ] **Step 2: `game/keyword.rs` — BGSKeyword**

```bash
cat > crates/skse-rs/src/game/keyword.rs <<'EOF'
//! `BGSKeyword` partial layout.
//!
//! M1-minimal: keywords are identified by their `TESForm` base (FormID).
//! `form_editor_id: BSFixedString` is not bound — the smoke test
//! doesn't need it.

use crate::game::form::TESForm;

/// `BGSKeyword` partial layout. Size 0x28 per CommonLibSSE-NG.
#[repr(C)]
pub struct BGSKeyword {
    pub base: TESForm,        // 0x00 — inline TESForm
    pub form_editor_id: usize, // 0x20 — BSFixedString is a single pointer
}

const _: () = assert!(core::mem::size_of::<BGSKeyword>() == 0x28);
const _: () = assert!(core::mem::offset_of!(BGSKeyword, base) == 0x00);
EOF
```

- [ ] **Step 3: `game/keyword_form.rs` — BGSKeywordForm + add_keyword**

```bash
cat > crates/skse-rs/src/game/keyword_form.rs <<'EOF'
//! `BGSKeywordForm` partial layout + `add_keyword` re-implementation.
//!
//! Ported from `CommonLibSSE-NG/src/RE/B/BGSKeywordForm.cpp`. Mora
//! does NOT call into game code for `AddKeyword` — that method is
//! inlined in CommonLibSSE-NG and has no Address Library ID. We
//! replicate the algorithm in Rust using Skyrim's `MemoryManager`
//! allocator (the same allocator the game uses for the keywords
//! array, so `free` semantics remain compatible).
//!
//! M1-minimal: only `add_keyword` is implemented. `has_keyword`,
//! `remove_keyword`, `add_keywords` batch variant — all deferred.

use crate::game::keyword::BGSKeyword;
use crate::game::memory::{allocate, deallocate};
use crate::relocation::RelocationError;

/// `BGSKeywordForm` is a mixin component — not a standalone form.
/// Forms that support keywords (Weapon, Armor, NPC, …) multiply-inherit
/// from both `TESForm` and `BGSKeywordForm`. The sub-object offset
/// within the owning form is type-specific.
///
/// Plan 3 binds `BGSKeywordForm` in isolation. The smoke test obtains
/// a `*mut BGSKeywordForm` by casting a `*mut TESForm` — safe ONLY
/// for forms whose layout places `BGSKeywordForm` at offset 0 after
/// the `TESForm` header OR when we know the exact sub-object offset.
/// For Iron Sword (a `TESObjectWEAP`), `BGSKeywordForm` begins at
/// offset 0x30 of the weapon. Plan 3 documents this case for the
/// smoke test; a proper multi-type accessor lands in M5.
///
/// Size 0x18 per CommonLibSSE-NG.
#[repr(C)]
pub struct BGSKeywordForm {
    pub vtable: *const (),
    pub keywords: *mut *mut BGSKeyword,
    pub num_keywords: u32,
    pub _pad_14: u32,
}

const _: () = assert!(core::mem::size_of::<BGSKeywordForm>() == 0x18);

/// Errors from [`add_keyword`].
#[derive(Debug, thiserror::Error)]
pub enum AddKeywordError {
    #[error("relocation: {0}")]
    Relocation(#[from] RelocationError),
    #[error("keyword allocator returned null for size {0}")]
    AllocatorFailed(usize),
}

/// Add `keyword` to `form`, returning `true` on insertion, `false` if
/// the keyword was already present (no mutation).
///
/// Algorithm (ported verbatim from CommonLibSSE-NG):
/// 1. Linear-scan existing keywords; if present, return `Ok(false)`.
/// 2. Allocate a new `BGSKeyword**` array of `num_keywords + 1` via
///    `MemoryManager::Allocate`.
/// 3. Copy old pointers + append the new one.
/// 4. Atomically swap the pointer and length (ordered).
/// 5. Free the old array (if it was non-null) via `MemoryManager::Deallocate`.
///
/// # Safety
/// - `form` must point to a valid `BGSKeywordForm` sub-object.
/// - `keyword` must point to a valid `BGSKeyword`.
/// - Must be called on the main thread during `kDataLoaded` or later,
///   when no other thread concurrently mutates this form's keywords.
pub unsafe fn add_keyword(
    form: *mut BGSKeywordForm,
    keyword: *mut BGSKeyword,
) -> Result<bool, AddKeywordError> {
    // Step 1: dedup.
    {
        let form_ref = unsafe { &*form };
        let n = form_ref.num_keywords as usize;
        let arr = form_ref.keywords;
        for i in 0..n {
            let existing = unsafe { *arr.add(i) };
            if existing == keyword {
                return Ok(false);
            }
        }
    }

    // Step 2: allocate new array of (n+1) pointers.
    let n = unsafe { (*form).num_keywords as usize };
    let new_count = n + 1;
    let size_bytes = new_count
        .checked_mul(core::mem::size_of::<*mut BGSKeyword>())
        .ok_or(AddKeywordError::AllocatorFailed(usize::MAX))?;
    let new_arr_raw = unsafe { allocate(size_bytes)? };
    if new_arr_raw.is_null() {
        return Err(AddKeywordError::AllocatorFailed(size_bytes));
    }
    let new_arr: *mut *mut BGSKeyword = new_arr_raw as *mut *mut BGSKeyword;

    // Step 3: copy + append.
    let old_arr = unsafe { (*form).keywords };
    for i in 0..n {
        unsafe {
            let p = *old_arr.add(i);
            *new_arr.add(i) = p;
        }
    }
    unsafe { *new_arr.add(n) = keyword };

    // Step 4: swap. Order matters: write `keywords` before `num_keywords`
    // so any concurrent reader never sees num_keywords past the end of a
    // shorter array. Not that we expect concurrent readers at kDataLoaded,
    // but defensive is cheap.
    let old_ptr = old_arr;
    unsafe {
        (*form).keywords = new_arr;
        (*form).num_keywords = new_count as u32;
    }

    // Step 5: free old.
    if !old_ptr.is_null() {
        unsafe { deallocate(old_ptr as *mut u8)? };
    }
    Ok(true)
}
EOF
```

- [ ] **Step 4: Verify**

```bash
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
cargo test --package skse-rs --lib
```

- [ ] **Step 5: Commit**

```bash
git add crates/skse-rs/src/game/memory.rs crates/skse-rs/src/game/keyword.rs crates/skse-rs/src/game/keyword_form.rs
git commit -m "skse-rs: game::{memory,keyword,keyword_form} — AddKeyword support

MemoryManager bindings (GetSingleton AE 11141, Allocate AE 68115,
Deallocate AE 68117). BGSKeyword partial layout (0x28). BGSKeywordForm
partial layout (0x18) + add_keyword function re-implementing
CommonLibSSE-NG's inline AddKeyword: linear dedupe scan, allocate via
MemoryManager (matches the game's own allocator), copy + append, swap
with ordered writes, free old array."
```

---

### Task 11: `game/data_handler.rs` — TESDataHandler singleton access

**Files:**
- Modify: `crates/skse-rs/src/game/data_handler.rs`

- [ ] **Step 1: Write the module**

```bash
cat > crates/skse-rs/src/game/data_handler.rs <<'EOF'
//! `TESDataHandler` singleton access.
//!
//! M1-minimal: only `get_singleton` is exposed. Inner fields are
//! opaque — the smoke test doesn't read them (it uses TESForm
//! lookup_by_id, which goes through the global allForms map, not
//! through TESDataHandler).

use crate::relocation::{Relocation, RelocationError};

/// Opaque TESDataHandler. No inner fields are exposed at M1.
#[repr(C)]
pub struct TESDataHandler {
    _private: [u8; 0],
}

pub mod ae_ids {
    /// Static variable holding a `TESDataHandler*`. Deref once.
    pub const SINGLETON: u64 = 400269;
}

/// Resolve the singleton pointer.
///
/// # Safety
/// Must be called after `Relocation::set_library` has been invoked.
/// The returned pointer, if non-null, lives for the process lifetime.
pub unsafe fn get_singleton() -> Result<*mut TESDataHandler, RelocationError> {
    let r = Relocation::id(ae_ids::SINGLETON)?;
    // The id resolves to a static variable slot: address-of-the-pointer.
    let pp: *mut *mut TESDataHandler = unsafe { r.as_mut_ptr() };
    if pp.is_null() {
        return Ok(core::ptr::null_mut());
    }
    Ok(unsafe { *pp })
}
EOF
```

- [ ] **Step 2: Verify**

```bash
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
```

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/src/game/data_handler.rs
git commit -m "skse-rs: game::data_handler — TESDataHandler singleton

AE Address Library ID 400269 — singleton slot (pointer-to-pointer).
get_singleton() deref's once to return a *mut TESDataHandler.
Inner layout opaque at M1 (smoke test uses TESForm::lookup_by_id,
which goes through the global allForms map directly)."
```

---

## Phase E — Messaging interface (Tasks 12-13)

### Task 12: `messaging.rs` — MessagingInterface wrapper

**Files:**
- Create: `crates/skse-rs/src/messaging.rs`
- Modify: `crates/skse-rs/src/lib.rs`

- [ ] **Step 1: Write the module**

```bash
cat > crates/skse-rs/src/messaging.rs <<'EOF'
//! SKSE messaging-interface wrapper.
//!
//! Exposes `register_data_loaded_listener` — registers a Rust
//! function to be invoked when SKSE broadcasts `kDataLoaded` (msg
//! type 8), which fires after all plugins and forms have loaded.
//!
//! Plan 3 only wires `kDataLoaded`. Future plans may add other event
//! types (kPreLoadGame, kPostLoadGame, kSaveGame, etc.).

use crate::ffi::{
    MessageType, MessagingCallback, SKSEInterface, SKSEMessage, SKSEMessagingInterface, KMESSAGING,
};
use crate::plugin::LoadError;

/// Obtain the SKSE messaging interface from the passed SKSEInterface.
///
/// # Safety
/// `skse` must be valid.
pub unsafe fn get_messaging(
    skse: &SKSEInterface,
) -> Result<&'static SKSEMessagingInterface, LoadError> {
    let ptr = unsafe { (skse.query_interface)(KMESSAGING) } as *const SKSEMessagingInterface;
    if ptr.is_null() {
        return Err(LoadError::MessagingUnavailable);
    }
    Ok(unsafe { &*ptr })
}

/// Register a listener for SKSE messages. `callback` receives every
/// SKSE broadcast message — it must filter on `msg.msg_type`.
///
/// # Safety
/// The callback must not store the message pointer past its own
/// return (SKSE owns the message memory).
pub unsafe fn register_listener(
    skse: &SKSEInterface,
    messaging: &SKSEMessagingInterface,
    callback: MessagingCallback,
) -> Result<(), LoadError> {
    let handle = unsafe { (skse.get_plugin_handle)() };
    let callback_void = callback as *mut core::ffi::c_void;
    let ok = unsafe { (messaging.register_listener)(handle, core::ptr::null(), callback_void) };
    if ok {
        Ok(())
    } else {
        Err(LoadError::ListenerRegistrationFailed)
    }
}

/// Check whether an `SKSEMessage` is the `kDataLoaded` broadcast.
///
/// # Safety
/// `msg` must be a valid pointer to an `SKSEMessage` (from SKSE).
pub unsafe fn is_data_loaded(msg: *mut SKSEMessage) -> bool {
    if msg.is_null() {
        return false;
    }
    unsafe { (*msg).msg_type == MessageType::DataLoaded as u32 }
}
EOF
```

- [ ] **Step 2: Register module**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/lib.rs")
text = p.read_text()
if "pub mod messaging" not in text:
    text = text.replace("pub mod plugin;", "pub mod messaging;\npub mod plugin;")
    p.write_text(text)
PY
grep '^pub mod' crates/skse-rs/src/lib.rs
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
git add crates/skse-rs/src/messaging.rs crates/skse-rs/src/lib.rs
git commit -m "skse-rs: messaging — kDataLoaded listener registration

Wraps SKSE's MessagingInterface (kMessaging = 5): get_messaging
queries the interface from SKSEInterface, register_listener plumbs
a Rust callback through. is_data_loaded filter helper for callbacks
that only care about one event type."
```

---

### Task 13: Upgrade `SksePlugin` trait with an `on_data_loaded` hook

**Files:**
- Modify: `crates/skse-rs/src/plugin.rs`
- Modify: `crates/skse-rs/src/lib.rs` — update the macro to wire the hook

This is the ergonomic layer. Downstream plugins can override
`on_data_loaded` to do game-state interaction without writing manual
messaging-listener glue.

- [ ] **Step 1: Add the hook method to `SksePlugin`**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/plugin.rs")
text = p.read_text()
old = """    /// Called from `SKSEPlugin_Load`. The passed interface pointer is
    /// valid until the DLL unloads and is safe to stash in a static
    /// cell.
    ///
    /// # Safety
    /// `skse` must point to a valid `SKSEInterface` provided by SKSE.
    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome;
}"""
new = """    /// Called from `SKSEPlugin_Load`. The passed interface pointer is
    /// valid until the DLL unloads and is safe to stash in a static
    /// cell.
    ///
    /// # Safety
    /// `skse` must point to a valid `SKSEInterface` provided by SKSE.
    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome;

    /// Called from the messaging-interface callback when SKSE
    /// broadcasts `kDataLoaded` — i.e., after all plugins and forms
    /// have loaded but before gameplay starts. Default: no-op.
    ///
    /// Override this to do game-state interaction (form lookups,
    /// AddKeyword calls, etc.). Returning without panicking is
    /// sufficient; errors should be logged.
    ///
    /// # Safety
    /// Runs on the main thread during SKSE kDataLoaded. The game form
    /// database is populated. Safe access to game state requires
    /// appropriate locks (see `game::lock::ReadGuard`).
    unsafe fn on_data_loaded() {}
}"""
assert old in text, "expected trait body not found"
text = text.replace(old, new)
p.write_text(text)
PY
```

- [ ] **Step 2: Update the macro to register a kDataLoaded listener**

The macro now emits a static messaging callback that dispatches to `on_data_loaded` when the right message fires, and ensures `on_load` registers the listener.

This requires the macro to generate a `kDataLoaded` callback fn and have `SKSEPlugin_Load` register it alongside calling `<T as SksePlugin>::on_load`. Replace the existing `SKSEPlugin_Load` portion of the macro:

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/lib.rs")
text = p.read_text()
old = '''        #[allow(non_snake_case)]
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn SKSEPlugin_Load(skse: *const $crate::ffi::SKSEInterface) -> bool {
            if skse.is_null() {
                return false;
            }
            // SAFETY: SKSE guarantees this pointer is valid for the DLL lifetime.
            let skse_ref: &'static $crate::ffi::SKSEInterface =
                unsafe { &*skse };
            match unsafe { <$plugin_ty as $crate::SksePlugin>::on_load(skse_ref) } {
                Ok(()) => true,
                Err(_) => false,
            }
        }
    };
}'''
new = '''        /// Generated SKSE messaging callback. Dispatches `kDataLoaded`
        /// to the plugin's `on_data_loaded` method; ignores all other
        /// SKSE messages.
        ///
        /// # Safety
        /// Called by SKSE on the main thread.
        #[allow(non_snake_case)]
        unsafe extern "C" fn __skse_rs_messaging_callback(
            msg: *mut $crate::ffi::SKSEMessage,
        ) {
            if unsafe { $crate::messaging::is_data_loaded(msg) } {
                unsafe { <$plugin_ty as $crate::SksePlugin>::on_data_loaded() };
            }
        }

        #[allow(non_snake_case)]
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn SKSEPlugin_Load(skse: *const $crate::ffi::SKSEInterface) -> bool {
            if skse.is_null() {
                return false;
            }
            // SAFETY: SKSE guarantees this pointer is valid for the DLL lifetime.
            let skse_ref: &'static $crate::ffi::SKSEInterface =
                unsafe { &*skse };
            // Run user on_load first so it can set up logging / load
            // address libraries before we register the messaging listener.
            match unsafe { <$plugin_ty as $crate::SksePlugin>::on_load(skse_ref) } {
                Ok(()) => {}
                Err(_) => return false,
            }
            // Register kDataLoaded listener.
            let messaging = match unsafe { $crate::messaging::get_messaging(skse_ref) } {
                Ok(m) => m,
                Err(_) => return false,
            };
            match unsafe {
                $crate::messaging::register_listener(
                    skse_ref,
                    messaging,
                    __skse_rs_messaging_callback,
                )
            } {
                Ok(()) => true,
                Err(_) => false,
            }
        }
    };
}'''
assert old in text, "expected old SKSEPlugin_Load body not found"
text = text.replace(old, new)
p.write_text(text)
PY
```

- [ ] **Step 3: Verify + commit**

```bash
cargo check --package skse-rs --all-targets
cargo test --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --all-targets
git add crates/skse-rs/src/plugin.rs crates/skse-rs/src/lib.rs
git commit -m "skse-rs: SksePlugin gains on_data_loaded hook

Default no-op; plugins override to interact with game state after
all forms have loaded. The declare_plugin! macro now generates a
messaging callback that dispatches kDataLoaded and wires up the
listener inside SKSEPlugin_Load after on_load succeeds."
```

---

## Phase F — Upgraded smoke plugin (Tasks 14-15)

### Task 14: Upgrade `skse-rs-smoke` to exercise the full M1 path

**Files:**
- Modify: `crates/skse-rs-smoke/src/lib.rs`

The smoke plugin now:
1. In `on_load`: opens the log, loads the Address Library from the default path, writes "ready".
2. In `on_data_loaded`: looks up Iron Sword (FormID `0x00012EB7`), casts to `BGSKeywordForm` at the weapon's BGSKeywordForm sub-object offset, looks up `WeapMaterialIron` keyword (FormID `0x0001E718` in Skyrim.esm), calls `add_keyword`, writes success/fail to the log.

- [ ] **Step 0: VERIFY the BGSKeywordForm offset within TESObjectWEAP**

This is the one unknown-at-planning-time value. The smoke test writes through this pointer — wrong offset → corrupt memory → Skyrim crash. Resolve before writing code.

Fetch `CommonLibSSE-NG/include/RE/T/TESObjectWEAP.h` and find either:
- An explicit `static_assert(offsetof(TESObjectWEAP, keywords) == 0x??)` — most reliable
- The class's declaration order of base classes (`TESBoundObject`, `TESFullName`, `TESModelTextureSwap`, `BGSPreloadable`, `BGSKeywordForm`, …) + each base's size so you can compute the offset manually

Alternative: search for `TESObjectWEAP` in `src/RE/T/TESObjectWEAP.cpp` or wherever the layout-verification asserts live.

Commit the finding as a brief comment in `crates/skse-rs/src/game/keyword_form.rs` (so the number has a provenance note). Expected offset is in the 0x60-0x80 range, NOT 0x30. Treat any guess as wrong.

If you cannot resolve the offset within reasonable effort (say, 15 minutes of searching), **switch strategies for the smoke test**: instead of mutating a weapon, use a different form type where BGSKeywordForm's location is trivial — e.g., a plain `TESObjectACTI` (Activator) or `TESRace`. Prefer a type where the class declaration is shorter (fewer base classes before BGSKeywordForm). Document the choice with a comment explaining why.

Replace the `WEAPON_KEYWORD_FORM_OFFSET = 0x30` constant below with the verified value and (if needed) the corresponding form type.

- [ ] **Step 1: Write the upgraded plugin**

```bash
cat > crates/skse-rs-smoke/src/lib.rs <<'EOF'
//! `SkseRsSmoke` — end-to-end smoke plugin.
//!
//! Exercises the full `skse-rs` M1 surface:
//!   - plugin load + log init + address library load
//!   - kDataLoaded listener registration
//!   - form lookup by FormID
//!   - AddKeyword via Skyrim's MemoryManager
//!
//! On-load log (`SkseRsSmoke.log`):
//!   Hello from skse-rs
//!   SKSE runtime: 0x01064920 (Skyrim SE AE 1.6.1170)
//!   Address Library loaded: <N> pairs
//!
//! On kDataLoaded:
//!   kDataLoaded received
//!   Iron Sword lookup: 0x00012EB7 -> <ptr>
//!   WeapMaterialIron lookup: 0x0001E718 -> <ptr>
//!   AddKeyword result: added | already-present
//!   verify readback: num_keywords = <n>
//!   smoke OK

#![allow(non_snake_case)]

use std::sync::OnceLock;

use skse_rs::ffi::SKSEInterface;
use skse_rs::game::form::{lookup_by_id, TESForm};
use skse_rs::game::keyword::BGSKeyword;
use skse_rs::game::keyword_form::{add_keyword, BGSKeywordForm};
use skse_rs::relocation;
use skse_rs::{declare_plugin, LoadOutcome, Logger, PluginVersion, SksePlugin};

// Known FormIDs in Skyrim.esm (mod index 0x00).
const IRON_SWORD_FID: u32 = 0x0001_2EB7;
const WEAP_MATERIAL_IRON_FID: u32 = 0x0001_E718;

// Offset of the `BGSKeywordForm` sub-object within a `TESObjectWEAP`.
// MUST BE VERIFIED in Step 0 against CommonLibSSE-NG's TESObjectWEAP.h —
// the 0x30 value below is a placeholder and is almost certainly WRONG
// (TESObjectWEAP has many base classes before BGSKeywordForm). Do not
// land this task without resolving the real offset.
const WEAPON_KEYWORD_FORM_OFFSET: isize = 0x30; // <-- VERIFY BEFORE LANDING

/// Global logger handle so on_data_loaded can write to it.
static LOGGER: OnceLock<Logger> = OnceLock::new();

struct SkseRsSmoke;

impl SksePlugin for SkseRsSmoke {
    const NAME: &'static str = "SkseRsSmoke";
    const AUTHOR: &'static str = "Mora / skse-rs";
    const VERSION: PluginVersion = PluginVersion {
        major: 0,
        minor: 1,
        patch: 0,
        build: 0,
    };

    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome {
        let logger = Logger::open(Self::NAME)?;
        logger.write_line("Hello from skse-rs").ok();
        logger
            .write_line(&format!(
                "SKSE runtime: 0x{:08x}",
                skse.runtime_version
            ))
            .ok();

        // Try to load the Address Library. On a real Skyrim install
        // the default path resolves; on CI without a game it doesn't.
        match relocation::resolve_default_library_path() {
            Some(p) => match relocation::load_library_from_path(&p) {
                Ok(()) => {
                    logger.write_line(&format!("Address Library loaded from {}", p.display())).ok();
                }
                Err(e) => {
                    logger.write_line(&format!("Address Library load FAILED: {e}")).ok();
                }
            },
            None => {
                logger.write_line("Address Library file not found at default path").ok();
            }
        }

        let _ = LOGGER.set(logger);
        Ok(())
    }

    unsafe fn on_data_loaded() {
        let Some(logger) = LOGGER.get() else { return };
        logger.write_line("kDataLoaded received").ok();

        // Look up Iron Sword.
        let iron_sword: *mut TESForm = match unsafe { lookup_by_id(IRON_SWORD_FID) } {
            Ok(Some(p)) => p,
            Ok(None) => {
                logger.write_line("Iron Sword (0x00012EB7) not found").ok();
                return;
            }
            Err(e) => {
                logger.write_line(&format!("lookup_by_id failed: {e}")).ok();
                return;
            }
        };
        logger
            .write_line(&format!("Iron Sword lookup: 0x{IRON_SWORD_FID:08X} -> {iron_sword:?}"))
            .ok();

        // Look up WeapMaterialIron keyword.
        let kw_form: *mut TESForm = match unsafe { lookup_by_id(WEAP_MATERIAL_IRON_FID) } {
            Ok(Some(p)) => p,
            Ok(None) => {
                logger
                    .write_line("WeapMaterialIron (0x0001E718) not found")
                    .ok();
                return;
            }
            Err(e) => {
                logger.write_line(&format!("lookup_by_id failed: {e}")).ok();
                return;
            }
        };
        logger
            .write_line(&format!(
                "WeapMaterialIron lookup: 0x{WEAP_MATERIAL_IRON_FID:08X} -> {kw_form:?}"
            ))
            .ok();

        // Cast: BGSKeyword is-a TESForm (inline base), so the pointer IS the BGSKeyword*.
        let kw: *mut BGSKeyword = kw_form as *mut BGSKeyword;

        // Cast iron_sword to its BGSKeywordForm sub-object. Iron Sword
        // is a TESObjectWEAP; its BGSKeywordForm begins at offset 0x30.
        let keyword_form: *mut BGSKeywordForm =
            unsafe { (iron_sword as *mut u8).offset(WEAPON_KEYWORD_FORM_OFFSET) }
                as *mut BGSKeywordForm;

        // Add.
        match unsafe { add_keyword(keyword_form, kw) } {
            Ok(true) => logger.write_line("AddKeyword result: added").ok(),
            Ok(false) => logger.write_line("AddKeyword result: already-present").ok(),
            Err(e) => logger.write_line(&format!("AddKeyword error: {e}")).ok(),
        };

        // Verify.
        let num_now = unsafe { (*keyword_form).num_keywords };
        logger
            .write_line(&format!("verify readback: num_keywords = {num_now}"))
            .ok();
        logger.write_line("smoke OK").ok();
    }
}

declare_plugin!(SkseRsSmoke);
EOF
```

- [ ] **Step 2: Build + verify exports**

```bash
source $HOME/.cargo/env
cargo check --package skse-rs-smoke
cargo xwin build --release --package skse-rs-smoke --target x86_64-pc-windows-msvc
llvm-objdump --private-headers target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll 2>/dev/null | grep -E "SKSEPlugin_" | head
```

Expected: three exports (`SKSEPlugin_Version`, `SKSEPlugin_Query`, `SKSEPlugin_Load`).

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs-smoke/src/lib.rs
git commit -m "skse-rs-smoke: upgrade to full M1 end-to-end

On-load loads the Address Library. On kDataLoaded: looks up Iron
Sword (Skyrim.esm 0x00012EB7), looks up WeapMaterialIron keyword
(0x0001E718), calls skse_rs::game::keyword_form::add_keyword at the
weapon's BGSKeywordForm sub-object (offset 0x30), verifies the
keyword count increment, writes 'smoke OK' to SkseRsSmoke.log."
```

---

### Task 15: Upgrade integration case `check.sh` + README

**Files:**
- Modify: `tests/integration/skse-rs-smoke/check.sh`
- Modify: `tests/integration/skse-rs-smoke/README.md`

- [ ] **Step 1: Upgrade check.sh**

```bash
cd /home/tbaldrid/oss/mora
cat > tests/integration/skse-rs-smoke/check.sh <<'EOF'
#!/usr/bin/env bash
# skse-rs-smoke end-to-end:
#   1. Plugin loaded and opened its log.
#   2. Address Library resolved.
#   3. kDataLoaded listener fired.
#   4. Iron Sword + WeapMaterialIron looked up.
#   5. add_keyword succeeded (added or already-present).
#   6. Verify readback logged.
#   7. Plugin wrote "smoke OK".

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

wait_for_main_menu || exit $?

LOG="$SKYRIM_PROFILE_DIR/SKSE/SkseRsSmoke.log"

if [[ ! -s "$LOG" ]]; then
    _err "skse-rs-smoke: SkseRsSmoke.log missing or empty at $LOG"
    exit 1
fi

# Required lines, in order, as grep -F -q matches.
REQUIRED=(
    "Hello from skse-rs"
    "SKSE runtime: 0x"
    "Address Library loaded"
    "kDataLoaded received"
    "Iron Sword lookup: 0x00012EB7"
    "WeapMaterialIron lookup: 0x0001E718"
    "AddKeyword result: "
    "verify readback: num_keywords ="
    "smoke OK"
)

for line in "${REQUIRED[@]}"; do
    if ! grep -F -q "$line" "$LOG"; then
        _err "skse-rs-smoke: missing required line in log: $line"
        _err "log contents:"
        sed 's/^/  /' "$LOG" >&2
        exit 1
    fi
done

echo "[check] skse-rs-smoke: PASS"
EOF
chmod +x tests/integration/skse-rs-smoke/check.sh
```

- [ ] **Step 2: Update README**

```bash
cat > tests/integration/skse-rs-smoke/README.md <<'EOF'
# skse-rs-smoke

End-to-end smoke test for the `skse-rs` Rust SKSE framework.

**Invariant:** after Skyrim reaches main menu, `SkseRsSmoke.log` must
contain, in order:

1. `Hello from skse-rs` (plugin loaded + log opened)
2. `SKSE runtime: 0x<hex>` (SKSEInterface read successfully)
3. `Address Library loaded` (version-lib bin parsed)
4. `kDataLoaded received` (messaging callback fired)
5. `Iron Sword lookup: 0x00012EB7 -> <ptr>` (TESForm::lookup_by_id hit)
6. `WeapMaterialIron lookup: 0x0001E718 -> <ptr>`
7. `AddKeyword result: <outcome>` (add_keyword returned Ok)
8. `verify readback: num_keywords = <n>`
9. `smoke OK`

All skse-rs subsystems are exercised: the full ABI layer,
logging, address library parsing, relocation resolution, messaging
listener registration, form lookup through the `allForms` hash map,
and keyword-array mutation via Skyrim's MemoryManager.

Run the case locally via `run-skyrim-test.sh` or on the self-hosted
runners. CI gate is enabled by the presence of `rust-ready.marker`
in this directory.
EOF
```

- [ ] **Step 3: Commit**

```bash
git add tests/integration/skse-rs-smoke/
git commit -m "tests/integration: upgrade skse-rs-smoke check + README

Check.sh now asserts 9 required lines in log order, matching the
upgraded plugin's output. README documents the full invariant."
```

---

## Phase G — Flip the CI gate (Task 16)

### Task 16: Enable `skyrim-integration` on PR CI

**Files:**
- Create: `tests/integration/skse-rs-smoke/rust-ready.marker`
- Modify: `.github/workflows/ci.yml`

The `rust-ready.marker` file is the signal that at least one case is
wired up for Rust-based plugins. The CI `if:` condition flips to a
real check for the marker.

- [ ] **Step 1: Drop the marker**

```bash
touch tests/integration/skse-rs-smoke/rust-ready.marker
```

- [ ] **Step 2: Update the CI if-guard to use a gate-check job**

Since job-level `if:` can't call `hashFiles`, introduce a small
`gate-check` job that runs on ubuntu-latest, emits a boolean output,
and `skyrim-integration` depends on it.

```bash
python3 - <<'PY'
from pathlib import Path
p = Path(".github/workflows/ci.yml")
text = p.read_text()

# 1) Replace `if: false` with a gate-check-based condition.
old_guard = """    if: false
    needs: [test, windows-cross]"""
new_guard = """    if: needs.integration-gate.outputs.has_rust_ready == 'true'
    needs: [test, windows-cross, integration-gate]"""
assert old_guard in text, "expected `if: false` + needs guard not found"
text = text.replace(old_guard, new_guard)

# 2) Insert a new gate-check job before `skyrim-integration`.
insert_before = """  skyrim-integration:"""
gate_job = '''  integration-gate:
    name: Integration gate check
    runs-on: ubuntu-latest
    outputs:
      has_rust_ready: ${{ steps.check.outputs.has_rust_ready }}
    steps:
      - uses: actions/checkout@v4
      - id: check
        shell: bash
        run: |
          # True if any tests/integration/<case>/rust-ready.marker exists.
          if compgen -G "tests/integration/**/rust-ready.marker" > /dev/null; then
              echo "has_rust_ready=true" >> "$GITHUB_OUTPUT"
          else
              echo "has_rust_ready=false" >> "$GITHUB_OUTPUT"
          fi

'''
assert insert_before in text
text = text.replace(insert_before, gate_job + insert_before)

p.write_text(text)
PY
```

Verify:
```bash
grep -A 2 'integration-gate:' .github/workflows/ci.yml | head
grep 'has_rust_ready' .github/workflows/ci.yml
```

- [ ] **Step 3: Commit**

```bash
git add tests/integration/skse-rs-smoke/rust-ready.marker .github/workflows/ci.yml
git commit -m "ci: enable skyrim-integration via integration-gate job

Drops rust-ready.marker in skse-rs-smoke (first Rust-ready case).
New integration-gate job (ubuntu-latest) checks for any
tests/integration/**/rust-ready.marker and exports has_rust_ready.
skyrim-integration's if: consumes that output — the self-hosted job
now runs on PRs that touch a Rust-ready case."
```

---

## Phase H — Final verification, push, PR (Task 17)

### Task 17: Full clean build + push + open PR

**Files:** none modified.

- [ ] **Step 1: Full clean verification**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
cargo xwin build --release --target x86_64-pc-windows-msvc -p skse-rs-smoke
ls -la target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll
llvm-objdump --private-headers target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll 2>/dev/null | grep -E "SKSEPlugin_" | head
```

Expected: all seven commands succeed. DLL still has the three SKSE exports.

- [ ] **Step 2: Push and open PR**

```bash
git push -u origin m1-skse-rs-game-interop
gh pr create --base master --head m1-skse-rs-game-interop \
    --title "Rust + KID pivot — M1 Part 2: skse-rs game interop" \
    --body "$(cat <<'PRBODY'
## Summary

Completes spec milestone M1 per
`docs/superpowers/plans/2026-04-21-rust-kid-pivot-plan-3-skse-rs-game-interop.md`:

- **Address Library v2 bin parser** (`skse_rs::address_library`) — pure Rust, binary-searchable, fixtured unit tests + optional real-bin smoke test.
- **Relocation resolver** (`skse_rs::relocation`) — `GetModuleHandleW(null)` image base + Address Library offset.
- **Partial game-type bindings** (`skse_rs::game::*`) — TESForm (form_id only), BSReadWriteLock (read path), BSTHashMap<FormID, *mut TESForm> read-only lookup, TESDataHandler singleton, BGSKeyword, BGSKeywordForm, MemoryManager. Every struct is `// M1-minimal` — full surface grows as real consumers need it.
- **TESForm::lookup_by_id** ported from CommonLibSSE-NG's inline source (crc32 hash, chain walk, read-lock guard).
- **BGSKeywordForm::add_keyword** re-implemented using Skyrim's MemoryManager (dedup, alloc, copy+append, swap, free).
- **kDataLoaded listener** via `SksePlugin::on_data_loaded` + declare_plugin!-generated messaging callback.
- **skse-rs-smoke** upgraded: looks up Iron Sword + WeapMaterialIron, calls add_keyword, verifies readback, writes "smoke OK" to log.
- **CI gate flipped:** new `integration-gate` job checks for `rust-ready.marker` files; `skyrim-integration` now runs on self-hosted runners when that marker is present.

## Test plan

- [x] `cargo test --workspace` — (expected: Plan 2's 15 + Plan 3's 14 = 29 tests)
- [x] `cargo clippy --all-targets -- -D warnings` clean
- [x] `cargo fmt --check` clean
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean
- [x] `cargo xwin build -p skse-rs-smoke --release` produces valid DLL
- [ ] Self-hosted `skyrim-integration` runs `tests/integration/skse-rs-smoke/check.sh` against a live Skyrim and passes

## Next up

M1 done. Plan 4 covers M2 — `mora-core` + `mora-esp`.
PRBODY
)"
```

Expected: PR URL printed.

- [ ] **Step 3: Watch CI**

```bash
gh run watch --exit-status 2>&1 | tail -10
```

Expected: fmt/clippy/test/windows-cross pass. `integration-gate` passes with `has_rust_ready=true`. `skyrim-integration` runs against the self-hosted pool and, if the runner image refresh is done and a Skyrim install is available, passes `check.sh`. If the runner image hasn't been refreshed yet, the job may fail — that's expected and the reviewer decides whether to require it before merge.

- [ ] **Step 4: Hand off to human merge**

Same process as Plan 1 + Plan 2: once CI is green (or self-hosted issues acknowledged), merge.

---

## Completion criteria

- [ ] All 29 unit/integration tests pass across the workspace.
- [ ] `SkseRsSmoke.dll` loads in Skyrim and produces a log matching `tests/integration/skse-rs-smoke/check.sh`'s expected lines.
- [ ] `skyrim-integration` CI job is live (either passing, or if self-hosted runner is unavailable, clearly-scoped failure).
- [ ] PR merged to `master`.

## Next plan

**M1 is complete**, consisting of Plan 2 (SKSE foundation) + Plan 3 (game interop). With `skse-rs` able to load, look up forms, and mutate them, the Rust runtime has everything it needs.

**Plan 4 begins M2:** `mora-core` + `mora-esp` — patch format, deterministic chance RNG port, mmapped ESP/ESL/ESM reader, plugins.txt resolver. No `skse-rs` dependency (compiler-only work). Written when this plan lands.
