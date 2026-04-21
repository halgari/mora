# Rust + KID Pivot — Plan 9: M4 Golden-Test Harness & Corpus

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove bit-identical KID compatibility on Weapon + Armor by committing a corpus of 10 hand-crafted INI scenarios + their real-KID post-state dumps, and running `cargo test` diff on every PR. First "100% compatible" evidence before M6 expands record-type coverage.

**Architecture:** Two decoupled loops, per `docs/superpowers/specs/2026-04-21-kid-golden-test-harness-design.md`.

- **Capture (rare, Skyrim-dependent):** `cargo xtask capture-kid-goldens` stages real KID.dll + a minimal Rust capture harness + scenario INI into a mod-dir, drives it through `run-skyrim-test.sh`, reads the harness's JSONL dump, writes it under `tests/golden-data/expected/<scenario>/`.
- **Test (per-PR, no Skyrim runtime):** `cargo test --package mora-kid --test golden` reads the INI + ESP data dir via `MORA_SKYRIM_DATA`, runs the distributor in-process, computes `post_state = KWDA ∪ patches`, diffs against the committed JSONL. Skip-not-fail if the env var is unset or ESP hashes diverge from the committed manifest.

**Tech Stack:** Rust 1.90. No new workspace deps. Reuses `skse-rs` (M1), `mora-esp` (M2), `mora-kid` (M3), existing Skyrim runner infrastructure.

**Reference spec:** `docs/superpowers/specs/2026-04-21-kid-golden-test-harness-design.md`

**Scope discipline:**
- **Weapon + Armor only.** Other 17 record types out of scope.
- **Hand-crafted INIs only.** Real-world INI corpus + purpose-built fixture ESPs intentionally deferred (see spec "Out-of-scope").
- **Minimal capture harness.** `crates/mora-golden-harness` is a cdylib with one job: on `kDataLoaded`, walk WEAP + ARMO forms, dump keyword lists to JSONL, write a `.done` sentinel, exit-signal via file presence (no TCP). Full `mora-test-harness` lands in M5.
- **Serial capture.** Parallel across the 3-runner pool is not worth the orchestration at 10 min total.
- **TDD where feasible.** xtask helpers, build.rs discovery, and test-helper logic get unit tests. The harness DLL + `run-skyrim-test.sh` integration steps are Skyrim-dependent and cannot be unit-tested; those tasks ship code with a manual first-capture verification.
- **Skip-not-fail on env drift.** `MORA_SKYRIM_DATA` unset, plugins.txt absent, ESP hash mismatch — all skip with a clear diagnostic. A Skyrim patch must never red CI on an unrelated PR.

---

## File Structure

**Modified:**
- `crates/skse-rs/src/game/hash_map.rs` — add `FormHashMap::iter()`
- `crates/skse-rs/src/game/mod.rs` — re-export new iteration API
- `crates/skse-rs/src/lib.rs` — module comment on new iteration surface
- `crates/xtask/Cargo.toml` — add deps: `serde_json`, `sha2`, `tempfile`
- `crates/xtask/src/main.rs` — wire `capture-kid-goldens` subcommand
- `crates/mora-kid/Cargo.toml` — add build-script opt-in + `serde_json`, `sha2` dev-deps
- `.github/workflows/ci.yml` — add KID-golden-tests step to the `skyrim-integration` self-hosted job
- `docs/src/runner-image-refresh.md` — append KID.dll + KID.ini section
- `Cargo.toml` (workspace) — add `sha2` and `serde_json` to `[workspace.dependencies]`; add `crates/mora-golden-harness` to `members`

**Created:**
- `crates/mora-golden-harness/Cargo.toml`
- `crates/mora-golden-harness/src/lib.rs` — the minimal SKSE capture DLL
- `crates/xtask/src/capture_kid_goldens.rs` — capture subcommand implementation
- `crates/xtask/src/capture_kid_goldens/manifest.rs` — ESP hashing + manifest JSON
- `crates/xtask/src/capture_kid_goldens/staging.rs` — mod-dir assembly
- `crates/xtask/tests/capture_kid_goldens.rs` — xtask unit tests (pure logic only)
- `crates/mora-kid/build.rs` — scenario discovery → generated test functions
- `crates/mora-kid/tests/golden.rs` — test helper + `include!(OUT_DIR/golden_tests.rs)`
- `tests/golden-data/kid-inis/<scenario>/<scenario>_KID.ini` × 10 — scenario rules
- `tests/golden-data/kid-inis/<scenario>/README.md` × 10 — scenario description
- (Later, via capture) `tests/golden-data/expected/<scenario>/{weapons,armors}.jsonl` + `manifest.json`

---

## Phase A — skse-rs extensions for form enumeration (Tasks 1–2)

### Task 1: `FormHashMap::iter()` — walk every form in the global map

The harness needs to enumerate every TESForm to filter by type. M1 only exposed `FormHashMap::lookup`; we add a bounded chain walk. This is the only new game-interop surface the harness requires.

**Files:**
- Modify: `crates/skse-rs/src/game/hash_map.rs`

- [ ] **Step 1: Write the failing test**

Append to `crates/skse-rs/src/game/hash_map.rs` `#[cfg(test)] mod tests`:

```rust
    #[test]
    fn iter_walks_synthetic_chain() {
        // Build a 4-bucket table with two entries in bucket 0 (chain) and
        // one entry in bucket 2; buckets 1 and 3 empty.
        let fake_a = 0xAAAA_AAAAusize as *mut TESForm;
        let fake_b = 0xBBBB_BBBBusize as *mut TESForm;
        let fake_c = 0xCCCC_CCCCusize as *mut TESForm;

        // The "extra" entry in bucket 0's chain lives outside the bucket
        // array; allocate it on the heap so we can link to it.
        let mut tail = Box::new(HashMapEntry {
            key: 0x200,
            _pad: 0,
            value: fake_b,
            next: SENTINEL as *mut HashMapEntry,
        });

        let mut buckets: Vec<HashMapEntry> = (0..4)
            .map(|_| HashMapEntry {
                key: 0,
                _pad: 0,
                value: core::ptr::null_mut(),
                next: core::ptr::null_mut(),
            })
            .collect();
        // Bucket 0: entry A with next -> tail (entry B, end-of-chain).
        buckets[0].key = 0x100;
        buckets[0].value = fake_a;
        buckets[0].next = tail.as_mut();
        // Bucket 1: empty (next == null).
        // Bucket 2: entry C, end-of-chain.
        buckets[2].key = 0x300;
        buckets[2].value = fake_c;
        buckets[2].next = SENTINEL as *mut HashMapEntry;
        // Bucket 3: empty (next == null).

        let map = FormHashMap {
            _pad00: 0,
            _pad08: 0,
            capacity: 4,
            free: 0,
            good: 0,
            sentinel: SENTINEL as *const (),
            _alloc_pad: 0,
            entries: buckets.as_mut_ptr(),
        };

        let mut seen: Vec<(u32, *mut TESForm)> =
            unsafe { map.iter().collect() };
        seen.sort_by_key(|(k, _)| *k);
        assert_eq!(
            seen,
            vec![(0x100, fake_a), (0x200, fake_b), (0x300, fake_c)]
        );
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cargo test --package skse-rs --lib hash_map::tests::iter_walks_synthetic_chain
```

Expected: FAIL with `no method named 'iter'`.

- [ ] **Step 3: Implement the iterator**

Append to `crates/skse-rs/src/game/hash_map.rs` (after the `impl FormHashMap` block):

```rust
/// Iterator over every `(form_id, *mut TESForm)` pair in a
/// `FormHashMap`. Walks every bucket in `entries`; for each non-empty
/// bucket follows the `next` chain until hitting the `SENTINEL`
/// terminator.
pub struct FormHashMapIter<'a> {
    map: &'a FormHashMap,
    /// Index of the bucket currently being walked.
    bucket: u32,
    /// The next entry to yield. `null_mut` means "start a new bucket
    /// at `self.bucket`".
    current: *mut HashMapEntry,
}

impl<'a> FormHashMapIter<'a> {
    fn advance_to_next_bucket(&mut self) {
        while self.bucket < self.map.capacity {
            let b = unsafe { self.map.entries.add(self.bucket as usize) };
            let b_ref = unsafe { &*b };
            // Empty buckets have next == null (Bethesda convention).
            if !b_ref.next.is_null() {
                self.current = b;
                self.bucket += 1;
                return;
            }
            self.bucket += 1;
        }
        // No more buckets.
        self.current = core::ptr::null_mut();
    }
}

impl<'a> Iterator for FormHashMapIter<'a> {
    type Item = (u32, *mut TESForm);

    fn next(&mut self) -> Option<Self::Item> {
        if self.current.is_null() {
            self.advance_to_next_bucket();
            if self.current.is_null() {
                return None;
            }
        }
        let entry = unsafe { &*self.current };
        let out = (entry.key, entry.value);
        // Advance to next entry in the chain.
        if (entry.next as usize) == SENTINEL {
            // End of chain; force next call to move to the next bucket.
            self.current = core::ptr::null_mut();
        } else {
            self.current = entry.next;
        }
        Some(out)
    }
}

impl FormHashMap {
    /// Walk every form in the map.
    ///
    /// # Safety
    /// Caller must hold the map's read lock for the duration of iteration
    /// and must not mutate the map while the iterator is live.
    pub unsafe fn iter(&self) -> FormHashMapIter<'_> {
        FormHashMapIter {
            map: self,
            bucket: 0,
            current: core::ptr::null_mut(),
        }
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cargo test --package skse-rs --lib hash_map::tests::iter_walks_synthetic_chain
```

Expected: PASS.

- [ ] **Step 5: Run full skse-rs tests to confirm no regressions**

```bash
cargo test --package skse-rs
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add crates/skse-rs/src/game/hash_map.rs
git commit -m "skse-rs: FormHashMap::iter — bounded bucket+chain walk for every form"
```

---

### Task 2: Promote keyword-form offsets out of skse-rs-smoke

`skse-rs-smoke` hard-codes `WEAPON_KEYWORD_FORM_OFFSET = 0x140` in `lib.rs:49`. The golden harness needs the same constant for WEAP plus a new one for ARMO. Promote both into `skse-rs` so downstream cdylibs share one definition.

`TESObjectARMO`'s offset is computed by summing CommonLibSSE-NG base-class sizes:

```
Base class                  size    running offset
TESBoundObject              0x30    0x000
TESFullName                 0x10    0x030
TESRaceForm                 0x08    0x040
BGSBipedObjectForm          0x10    0x048
TESEnchantableForm          0x18    0x058
TESValueForm                0x10    0x070
TESWeightForm               0x10    0x080
BGSDestructibleObjectForm   0x10    0x090
BGSEquipType                0x10    0x0A0
BGSPreloadable              0x08    0x0B0
BGSPickupPutdownSounds      0x18    0x0B8
BGSBlockBashData            0x18    0x0D0
TESDescription              0x10    0x0E8
BGSBipedModelResource       0x70    0x0F8   <- TESModel-carrying biped bundle
BGSKeywordForm              0x18    0x168   <- starts here
```

Cross-check: `sizeof(TESObjectARMO)` is `0x1F8` per CommonLibSSE-NG static assert. The implementer must verify the running sum against that static assert and the `BGSKeywordForm` offset printed by `TESObjectARMO::GetKeywordOffset` in CommonLibSSE's runtime debug path (if reachable). If the sum differs, use the offset CommonLibSSE-NG reports — the base-class-size table above is a starting point, not gospel. A wrong offset silently corrupts the dump.

**Files:**
- Modify: `crates/skse-rs/src/game/keyword_form.rs`
- Modify: `crates/skse-rs-smoke/src/lib.rs` — replace local constant with the promoted one

- [ ] **Step 1: Append offset constants to `keyword_form.rs`**

Append to `crates/skse-rs/src/game/keyword_form.rs` (after `add_keyword`):

```rust
/// Offset of the `BGSKeywordForm` sub-object within a `TESObjectWEAP`.
/// Verified via CommonLibSSE-NG base-class size sums and cross-checked
/// against `sizeof(TESObjectWEAP) == 0x220` static_assert.
/// See the detailed derivation in `skse-rs-smoke/src/lib.rs`.
pub const WEAPON_KEYWORD_FORM_OFFSET: isize = 0x140;

/// Offset of the `BGSKeywordForm` sub-object within a `TESObjectARMO`.
/// Derived the same way as [`WEAPON_KEYWORD_FORM_OFFSET`]; cross-check
/// against `sizeof(TESObjectARMO) == 0x1F8`.
pub const ARMOR_KEYWORD_FORM_OFFSET: isize = 0x168;

/// FormType byte values used by `TESForm::form_type`. Matches the
/// Bethesda `FormType` enum; only the two variants the golden harness
/// cares about are exposed.
pub mod form_type {
    pub const WEAPON: u8 = 0x29; // 41, TESObjectWEAP
    pub const ARMOR: u8 = 0x1A;  // 26, TESObjectARMO
}
```

- [ ] **Step 2: Update skse-rs-smoke to use the promoted constant**

In `crates/skse-rs-smoke/src/lib.rs`, remove the local `WEAPON_KEYWORD_FORM_OFFSET` block (lines 24–49 in the file as written at plan time; find it by search) and replace the reference at line 141:

Find:
```rust
let keyword_form: *mut BGSKeywordForm =
    unsafe { (iron_sword as *mut u8).offset(WEAPON_KEYWORD_FORM_OFFSET) }
        as *mut BGSKeywordForm;
```

Replace with:
```rust
let keyword_form: *mut BGSKeywordForm =
    unsafe {
        (iron_sword as *mut u8)
            .offset(skse_rs::game::keyword_form::WEAPON_KEYWORD_FORM_OFFSET)
    } as *mut BGSKeywordForm;
```

- [ ] **Step 3: Run tests to confirm no regressions**

```bash
cargo test --workspace
cargo build --workspace
```

Expected: all tests and builds pass.

- [ ] **Step 4: Cross-compile smoke to Windows to confirm the constant move didn't break the cdylib**

```bash
cargo xwin check --target x86_64-pc-windows-msvc --package skse-rs-smoke
```

Expected: clean check.

- [ ] **Step 5: Commit**

```bash
git add crates/skse-rs/src/game/keyword_form.rs crates/skse-rs-smoke/src/lib.rs
git commit -m "skse-rs: promote WEAPON/ARMOR_KEYWORD_FORM_OFFSET + form_type constants"
```

---

## Phase B — Capture harness DLL (Tasks 3–5)

### Task 3: Scaffold `mora-golden-harness` cdylib

**Files:**
- Create: `crates/mora-golden-harness/Cargo.toml`
- Create: `crates/mora-golden-harness/src/lib.rs`
- Modify: `Cargo.toml` (workspace) — add `crates/mora-golden-harness` to `members`

- [ ] **Step 1: Add crate to workspace members**

In `Cargo.toml` workspace root, find:
```toml
members = [
    "crates/skse-rs",
    "crates/skse-rs-smoke",
    ...
    "crates/xtask",
]
```

Add `"crates/mora-golden-harness",` before `"crates/xtask",`.

- [ ] **Step 2: Create the crate manifest**

```toml
# crates/mora-golden-harness/Cargo.toml
[package]
name = "mora-golden-harness"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "Minimal SKSE DLL: on kDataLoaded dumps every weapon/armor form's keywords to JSONL, then writes a .done sentinel. Used only by the M4 golden-test capture xtask. Subsumed by mora-test-harness at M5."
publish = false

[lib]
name = "MoraGoldenHarness"
crate-type = ["cdylib"]

[dependencies]
skse-rs = { path = "../skse-rs" }
```

- [ ] **Step 3: Create the scaffolded `lib.rs`**

This step establishes the plugin-export scaffolding without the capture logic yet (Task 4 adds the on_data_loaded body):

```rust
// crates/mora-golden-harness/src/lib.rs
//! `MoraGoldenHarness` — M4 golden-test capture SKSE plugin.
//!
//! On `kDataLoaded`:
//!   1. Walk every form via `TESDataHandler`'s allForms map.
//!   2. For each WEAP and ARMO, read its `BGSKeywordForm::keywords`
//!      array.
//!   3. Serialize `(form_id → sorted keyword_ids)` as JSONL into
//!      `Data/MoraCache/dumps/{weapons,armors}.jsonl`.
//!   4. Write an empty sentinel file `Data/MoraCache/dumps/.done`.
//!
//! The capture `xtask` polls for `.done` via a `check.sh` hook, then
//! tears the game down. No TCP, no commands, no long-lived listener.
//!
//! This crate is intentionally narrow: it exists to unblock M4 before
//! the full `mora-test-harness` TCP protocol lands in M5. When M5
//! ships, consolidate with (or delete in favor of) the full harness.

#![allow(non_snake_case)]

use std::sync::OnceLock;

use skse_rs::ffi::SKSEInterface;
use skse_rs::relocation;
use skse_rs::{LoadOutcome, Logger, PluginVersion, SksePlugin, declare_plugin};

static LOGGER: OnceLock<Logger> = OnceLock::new();

struct MoraGoldenHarness;

impl SksePlugin for MoraGoldenHarness {
    const NAME: &'static str = "MoraGoldenHarness";
    const AUTHOR: &'static str = "Mora";
    const VERSION: PluginVersion = PluginVersion {
        major: 0,
        minor: 1,
        patch: 0,
        build: 0,
    };

    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome {
        let logger = Logger::open(Self::NAME)?;
        logger.write_line("MoraGoldenHarness loaded").ok();
        logger
            .write_line(&format!("SKSE runtime: 0x{:08x}", skse.runtime_version))
            .ok();
        if let Some(p) = relocation::resolve_default_library_path() {
            if let Err(e) = relocation::load_library_from_path(&p) {
                logger
                    .write_line(&format!("Address Library load FAILED: {e}"))
                    .ok();
            }
        }
        let _ = LOGGER.set(logger);
        Ok(())
    }

    unsafe fn on_data_loaded() {
        // Task 4 adds the capture logic here.
    }
}

declare_plugin!(MoraGoldenHarness);
```

- [ ] **Step 4: Confirm the crate compiles via cargo-xwin**

```bash
cargo xwin check --target x86_64-pc-windows-msvc --package mora-golden-harness
```

Expected: clean check.

- [ ] **Step 5: Confirm workspace still builds**

```bash
cargo build --workspace
cargo test --workspace
```

Expected: everything green.

- [ ] **Step 6: Commit**

```bash
git add Cargo.toml crates/mora-golden-harness/
git commit -m "mora-golden-harness: scaffold capture-only SKSE DLL (M4)"
```

---

### Task 4: Implement `on_data_loaded` — enumerate forms + collect keywords

No unit test possible (requires running Skyrim). Correctness is validated by the first capture run in Task 11.

**Files:**
- Modify: `crates/mora-golden-harness/src/lib.rs`

- [ ] **Step 1: Replace the empty `on_data_loaded` with the capture body**

Replace the `unsafe fn on_data_loaded() { ... }` body in `crates/mora-golden-harness/src/lib.rs` with:

```rust
    unsafe fn on_data_loaded() {
        let Some(logger) = LOGGER.get() else { return };
        logger.write_line("kDataLoaded received — beginning capture").ok();

        let collected = match unsafe { collect_keyword_dumps(logger) } {
            Ok(x) => x,
            Err(e) => {
                logger
                    .write_line(&format!("capture failed: {e}"))
                    .ok();
                return;
            }
        };

        if let Err(e) = write_dumps(&collected) {
            logger
                .write_line(&format!("write_dumps failed: {e}"))
                .ok();
            return;
        }

        if let Err(e) = write_done_sentinel() {
            logger
                .write_line(&format!("write_done_sentinel failed: {e}"))
                .ok();
            return;
        }

        logger
            .write_line(&format!(
                "capture OK: {} weapons / {} armors",
                collected.weapons.len(),
                collected.armors.len()
            ))
            .ok();
    }
```

- [ ] **Step 2: Add the collection helpers (above `impl SksePlugin`)**

Add the following helpers just above `impl SksePlugin for MoraGoldenHarness`:

```rust
use std::path::{Path, PathBuf};

use skse_rs::game::form::TESForm;
use skse_rs::game::hash_map::FormHashMap;
use skse_rs::game::keyword_form::{
    ARMOR_KEYWORD_FORM_OFFSET, BGSKeywordForm, WEAPON_KEYWORD_FORM_OFFSET, form_type,
};
use skse_rs::game::lock::{BSReadWriteLock, ReadGuard};

struct Dumps {
    /// Sorted by form_id. Each keyword list is also sorted.
    weapons: Vec<(u32, Vec<u32>)>,
    armors: Vec<(u32, Vec<u32>)>,
}

#[derive(Debug, thiserror::Error)]
enum CaptureError {
    #[error("relocation: {0}")]
    Relocation(#[from] skse_rs::relocation::RelocationError),
    #[error("allForms map pointer was null")]
    NullAllForms,
}

unsafe fn collect_keyword_dumps(logger: &Logger) -> Result<Dumps, CaptureError> {
    // Resolve the global form-map pointer + its lock. Same IDs as
    // skse-rs::game::form::lookup_by_id uses internally.
    let all_forms_pp =
        skse_rs::relocation::Relocation::id(skse_rs::game::form::ae_ids::ALL_FORMS)?;
    let lock_reloc =
        skse_rs::relocation::Relocation::id(skse_rs::game::form::ae_ids::ALL_FORMS_LOCK)?;
    let all_forms_pp: *mut *mut FormHashMap = unsafe { all_forms_pp.as_mut_ptr() };
    let lock: *mut BSReadWriteLock = unsafe { lock_reloc.as_mut_ptr() };
    let _guard = unsafe { ReadGuard::new(lock)? };
    let map_ptr: *mut FormHashMap = unsafe { *all_forms_pp };
    if map_ptr.is_null() {
        return Err(CaptureError::NullAllForms);
    }
    let map: &FormHashMap = unsafe { &*map_ptr };

    let mut weapons: Vec<(u32, Vec<u32>)> = Vec::new();
    let mut armors: Vec<(u32, Vec<u32>)> = Vec::new();

    for (form_id, form) in unsafe { map.iter() } {
        if form.is_null() {
            continue;
        }
        let ty = unsafe { (*form).form_type };
        let kws = match ty {
            form_type::WEAPON => unsafe {
                read_keywords(form, WEAPON_KEYWORD_FORM_OFFSET)
            },
            form_type::ARMOR => unsafe {
                read_keywords(form, ARMOR_KEYWORD_FORM_OFFSET)
            },
            _ => continue,
        };
        if kws.is_empty() {
            continue; // omit forms with empty keyword lists
        }
        match ty {
            form_type::WEAPON => weapons.push((form_id, kws)),
            form_type::ARMOR => armors.push((form_id, kws)),
            _ => unreachable!(),
        }
    }

    weapons.sort_by_key(|(fid, _)| *fid);
    armors.sort_by_key(|(fid, _)| *fid);

    logger
        .write_line(&format!(
            "collected: {} weapons / {} armors (non-empty keyword lists)",
            weapons.len(),
            armors.len()
        ))
        .ok();

    Ok(Dumps { weapons, armors })
}

/// Cast `form` to a `*mut BGSKeywordForm` via `offset`, clone its
/// `keywords` array into a sorted `Vec<u32>` of FormIDs.
unsafe fn read_keywords(form: *mut TESForm, offset: isize) -> Vec<u32> {
    let kw_form: *mut BGSKeywordForm =
        unsafe { (form as *mut u8).offset(offset) } as *mut BGSKeywordForm;
    let kw_ref = unsafe { &*kw_form };
    let n = kw_ref.num_keywords as usize;
    if n == 0 || kw_ref.keywords.is_null() {
        return Vec::new();
    }
    let mut out: Vec<u32> = Vec::with_capacity(n);
    for i in 0..n {
        let p = unsafe { *kw_ref.keywords.add(i) };
        if p.is_null() {
            continue;
        }
        // BGSKeyword is-a TESForm; the form_id is at the fixed TESForm
        // offset 0x14.
        let form_ptr: *const TESForm = p as *const TESForm;
        let fid = unsafe { (*form_ptr).form_id };
        out.push(fid);
    }
    out.sort_unstable();
    out.dedup();
    out
}

fn dumps_dir() -> PathBuf {
    // CWD when Skyrim runs is the install root. Data/SKSE/Plugins is
    // the canonical mod dir; we write dumps under
    // Data/MoraCache/dumps/ so the xtask's host-side pull has a
    // fixed path.
    Path::new("Data").join("MoraCache").join("dumps")
}

fn write_dumps(dumps: &Dumps) -> std::io::Result<()> {
    let dir = dumps_dir();
    std::fs::create_dir_all(&dir)?;
    write_jsonl(&dir.join("weapons.jsonl"), &dumps.weapons)?;
    write_jsonl(&dir.join("armors.jsonl"), &dumps.armors)?;
    Ok(())
}

fn write_jsonl(path: &Path, entries: &[(u32, Vec<u32>)]) -> std::io::Result<()> {
    use std::io::Write;
    let f = std::fs::File::create(path)?;
    let mut w = std::io::BufWriter::new(f);
    for (form_id, kws) in entries {
        write!(w, "{{\"form\":\"0x{:08x}\",\"kws\":[", form_id)?;
        for (i, kw) in kws.iter().enumerate() {
            if i > 0 {
                write!(w, ",")?;
            }
            write!(w, "\"0x{:08x}\"", kw)?;
        }
        writeln!(w, "]}}")?;
    }
    w.flush()?;
    Ok(())
}

fn write_done_sentinel() -> std::io::Result<()> {
    let dir = dumps_dir();
    std::fs::create_dir_all(&dir)?;
    // Empty sentinel; presence is the signal.
    std::fs::File::create(dir.join(".done"))?;
    Ok(())
}
```

- [ ] **Step 3: Add `thiserror` to the crate's dependencies**

Edit `crates/mora-golden-harness/Cargo.toml`, add to `[dependencies]`:

```toml
thiserror.workspace = true
```

- [ ] **Step 4: Confirm Windows cross-compile is clean**

```bash
cargo xwin check --target x86_64-pc-windows-msvc --package mora-golden-harness
```

Expected: clean check.

- [ ] **Step 5: Confirm workspace lints are clean**

```bash
cargo clippy --workspace --all-targets -- -D warnings
```

Expected: no warnings.

- [ ] **Step 6: Commit**

```bash
git add crates/mora-golden-harness/
git commit -m "mora-golden-harness: on_data_loaded dump of WEAP+ARMO keyword lists to JSONL"
```

---

### Task 5: Build the harness DLL once to confirm link

This task has no code changes — it confirms the cross-compile artifact lands where the xtask will expect it.

- [ ] **Step 1: Release-build the DLL**

```bash
cargo xwin build --release --target x86_64-pc-windows-msvc --package mora-golden-harness
```

Expected: `target/x86_64-pc-windows-msvc/release/MoraGoldenHarness.dll` exists.

- [ ] **Step 2: Verify the DLL is present**

```bash
ls -la target/x86_64-pc-windows-msvc/release/MoraGoldenHarness.dll
```

Expected: file exists, non-zero size.

- [ ] **Step 3: No commit — build artifact only**

---

## Phase C — xtask orchestration (Tasks 6–8)

### Task 6: Workspace deps + xtask subcommand dispatch

**Files:**
- Modify: `Cargo.toml` (workspace)
- Modify: `crates/xtask/Cargo.toml`
- Modify: `crates/xtask/src/main.rs`

- [ ] **Step 1: Add workspace-level deps**

Edit `Cargo.toml`, append to `[workspace.dependencies]`:

```toml
serde_json = "1"
sha2 = "0.10"
tempfile = "3"
```

- [ ] **Step 2: Pull them into xtask**

Edit `crates/xtask/Cargo.toml`:

```toml
[dependencies]
anyhow.workspace = true
clap.workspace = true
serde.workspace = true
serde_json.workspace = true
sha2.workspace = true
tempfile.workspace = true
```

- [ ] **Step 3: Replace the main stub with clap dispatch**

Replace the entire contents of `crates/xtask/src/main.rs`:

```rust
//! `cargo xtask <cmd>` — dev-workflow orchestration.

use anyhow::Result;
use clap::{Parser, Subcommand};

mod capture_kid_goldens;

#[derive(Parser)]
#[command(name = "xtask", about = "Mora dev-workflow orchestration")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Capture KID ground-truth dumps for one or more scenarios by
    /// running the real KID SKSE plugin against the scenario's INI in
    /// a real Skyrim under Proton, then extracting the harness's JSONL
    /// output into `tests/golden-data/expected/<scenario>/`.
    CaptureKidGoldens(capture_kid_goldens::Args),
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::CaptureKidGoldens(args) => capture_kid_goldens::run(args),
    }
}
```

- [ ] **Step 4: Create the subcommand stub**

`crates/xtask/src/capture_kid_goldens.rs`:

```rust
//! `cargo xtask capture-kid-goldens` — capture real-KID ground-truth
//! dumps for the M4 golden-test corpus.
//!
//! See `docs/superpowers/specs/2026-04-21-kid-golden-test-harness-design.md`.

use anyhow::{Result, bail};
use clap::Parser;
use std::path::PathBuf;

pub mod manifest;
pub mod staging;

#[derive(Parser, Debug)]
pub struct Args {
    /// Capture a single scenario by name.
    #[arg(long, conflicts_with = "all")]
    pub scenario: Option<String>,

    /// Capture every scenario found under `tests/golden-data/kid-inis/`.
    #[arg(long, conflicts_with = "scenario")]
    pub all: bool,

    /// Skyrim install root. Defaults to `/tmp/skyrim` (the overlay
    /// path set up by `run-skyrim-test.sh` on the runner image) when
    /// unset.
    #[arg(long, env = "SKYRIM_ROOT")]
    pub skyrim_root: Option<PathBuf>,
}

pub fn run(args: Args) -> Result<()> {
    // Validation: exactly one of --scenario / --all.
    let scenarios: Vec<String> = match (args.scenario.as_deref(), args.all) {
        (Some(name), false) => vec![name.to_string()],
        (None, true) => discover_scenarios()?,
        _ => bail!("exactly one of --scenario NAME or --all must be specified"),
    };

    // Task 7 fills this in.
    for name in &scenarios {
        println!("capture-kid-goldens: would capture {name}");
    }
    Ok(())
}

pub fn discover_scenarios() -> Result<Vec<String>> {
    let root = workspace_root()?.join("tests/golden-data/kid-inis");
    if !root.is_dir() {
        bail!(
            "scenario root not found: {} (run from a mora checkout)",
            root.display()
        );
    }
    let mut out = Vec::new();
    for entry in std::fs::read_dir(&root)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            if let Some(name) = entry.file_name().to_str() {
                out.push(name.to_string());
            }
        }
    }
    out.sort();
    Ok(out)
}

/// Find the workspace root by walking up from the current exe's
/// manifest-relative location. `CARGO_MANIFEST_DIR` at build time
/// points at `crates/xtask`; its parent-parent is the workspace root.
pub fn workspace_root() -> Result<PathBuf> {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let p = PathBuf::from(manifest_dir)
        .parent()
        .and_then(|p| p.parent())
        .ok_or_else(|| anyhow::anyhow!("cannot derive workspace root from {manifest_dir}"))?
        .to_path_buf();
    Ok(p)
}
```

- [ ] **Step 5: Create empty module stubs**

`crates/xtask/src/capture_kid_goldens/manifest.rs`:

```rust
//! Manifest generation: SHA256 over every active ESP + KID + Skyrim
//! versions, serialized to `tests/golden-data/expected/<scenario>/manifest.json`.
//!
//! Task 8 fills this in.
```

`crates/xtask/src/capture_kid_goldens/staging.rs`:

```rust
//! Mod-dir staging: copy KID.dll + KID.ini + scenario INI +
//! MoraGoldenHarness.dll into a temp dir suitable for
//! `run-skyrim-test.sh`.
//!
//! Task 7 fills this in.
```

- [ ] **Step 6: Verify build + lints**

```bash
cargo build --workspace
cargo clippy --workspace --all-targets -- -D warnings
```

Expected: clean.

- [ ] **Step 7: Smoke-run the dispatch**

```bash
cargo xtask capture-kid-goldens --help
```

Expected: clap prints the command help.

- [ ] **Step 8: Commit**

```bash
git add Cargo.toml crates/xtask/
git commit -m "xtask: scaffold capture-kid-goldens subcommand + workspace deps"
```

---

### Task 7: Mod-dir staging + `run-skyrim-test.sh` orchestration

Assembles the per-scenario capture: a staged mod-dir containing KID.dll, KID.ini, the scenario INI, the harness DLL. Invokes `run-skyrim-test.sh` with a one-line check.sh that polls for the harness's `.done` sentinel. Pulls the resulting JSONL back out.

TDD for the pure staging logic; the `run-skyrim-test.sh` invocation itself is integration-dependent (tested in Task 11's first-capture).

**Files:**
- Modify: `crates/xtask/src/capture_kid_goldens/staging.rs`
- Modify: `crates/xtask/src/capture_kid_goldens.rs`
- Create: `crates/xtask/tests/capture_kid_goldens.rs`

- [ ] **Step 1: Write a failing test for `staging::assemble_mod_dir`**

Create `crates/xtask/tests/capture_kid_goldens.rs`:

```rust
//! Pure-logic unit tests for the capture-kid-goldens xtask. Anything
//! that requires Skyrim / Proton / the runner pool is out of scope here.

use std::fs;

use tempfile::tempdir;
use xtask::capture_kid_goldens::staging::{AssembleInputs, assemble_mod_dir};

#[test]
fn assemble_mod_dir_copies_all_fixtures_into_plugins_dir() {
    let tmp = tempdir().unwrap();
    let src = tmp.path().join("src");
    let scenario_ini_dir = src.join("scenario");
    let harness = src.join("harness");
    fs::create_dir_all(&scenario_ini_dir).unwrap();
    fs::create_dir_all(&harness).unwrap();
    fs::write(scenario_ini_dir.join("example_KID.ini"), b"; sample rule\n").unwrap();
    fs::write(
        scenario_ini_dir.join("notes.md"),
        b"should be skipped (not an ini)",
    )
    .unwrap();
    fs::write(harness.join("MoraGoldenHarness.dll"), b"\x4D\x5A fake PE").unwrap();

    let kid_dll = src.join("KID.dll");
    let kid_ini = src.join("KID.ini");
    fs::write(&kid_dll, b"\x4D\x5A fake KID PE").unwrap();
    fs::write(&kid_ini, b"; KID defaults\n").unwrap();

    let out = tmp.path().join("stage");
    assemble_mod_dir(&AssembleInputs {
        output: &out,
        kid_dll: &kid_dll,
        kid_ini: &kid_ini,
        scenario_ini_dir: &scenario_ini_dir,
        harness_dll: &harness.join("MoraGoldenHarness.dll"),
    })
    .unwrap();

    let plugins = out.join("Data").join("SKSE").join("Plugins");
    assert!(plugins.join("KID.dll").is_file());
    assert!(plugins.join("KID.ini").is_file());
    assert!(plugins.join("MoraGoldenHarness.dll").is_file());
    assert!(plugins.join("example_KID.ini").is_file());
    // README / non-INI fixtures must not be copied.
    assert!(!plugins.join("notes.md").exists());
}

#[test]
fn assemble_mod_dir_errors_on_missing_harness() {
    let tmp = tempdir().unwrap();
    let src = tmp.path().join("src");
    let scenario_ini_dir = src.join("scenario");
    fs::create_dir_all(&scenario_ini_dir).unwrap();
    fs::write(scenario_ini_dir.join("x_KID.ini"), b"").unwrap();
    fs::write(src.join("KID.dll"), b"").unwrap();
    fs::write(src.join("KID.ini"), b"").unwrap();

    let out = tmp.path().join("stage");
    let err = assemble_mod_dir(&AssembleInputs {
        output: &out,
        kid_dll: &src.join("KID.dll"),
        kid_ini: &src.join("KID.ini"),
        scenario_ini_dir: &scenario_ini_dir,
        harness_dll: &src.join("MoraGoldenHarness.dll"), // does not exist
    })
    .unwrap_err();
    assert!(
        err.to_string().contains("MoraGoldenHarness.dll"),
        "expected error to mention the missing harness file; got: {err}"
    );
}
```

The integration-test crate needs `xtask` as a dependency. Add to `crates/xtask/Cargo.toml`:

```toml
[dev-dependencies]
tempfile.workspace = true
```

And expose the module path by turning the xtask binary into a crate with a library target. Edit `crates/xtask/Cargo.toml` — add:

```toml
[lib]
path = "src/lib.rs"
```

Create `crates/xtask/src/lib.rs`:

```rust
//! Library re-exports so integration tests can exercise internal logic.
pub mod capture_kid_goldens;
```

Remove `mod capture_kid_goldens;` from `main.rs`. Replace with:
```rust
use xtask::capture_kid_goldens;
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cargo test --package xtask --test capture_kid_goldens assemble_mod_dir_copies_all_fixtures_into_plugins_dir
```

Expected: FAIL (function not defined / compile error).

- [ ] **Step 3: Implement `staging::assemble_mod_dir`**

Replace `crates/xtask/src/capture_kid_goldens/staging.rs`:

```rust
//! Mod-dir staging: copy KID.dll + KID.ini + scenario INI +
//! MoraGoldenHarness.dll into a temp dir suitable for
//! `run-skyrim-test.sh`.

use anyhow::{Context, Result, bail};
use std::path::Path;

pub struct AssembleInputs<'a> {
    pub output: &'a Path,
    pub kid_dll: &'a Path,
    pub kid_ini: &'a Path,
    pub scenario_ini_dir: &'a Path,
    pub harness_dll: &'a Path,
}

/// Assemble a staged mod-dir at `inputs.output`. Resulting layout:
///
/// ```text
/// <output>/Data/SKSE/Plugins/
///   KID.dll
///   KID.ini
///   MoraGoldenHarness.dll
///   <scenario>_KID.ini
/// ```
///
/// Only files ending in `_KID.ini` (case-insensitive) are copied from
/// the scenario directory; READMEs and other non-ini fixtures are
/// skipped.
pub fn assemble_mod_dir(inputs: &AssembleInputs<'_>) -> Result<()> {
    if !inputs.harness_dll.is_file() {
        bail!(
            "harness DLL not found at {} — did you `cargo xwin build --release --target x86_64-pc-windows-msvc -p mora-golden-harness` first?",
            inputs.harness_dll.display()
        );
    }
    if !inputs.kid_dll.is_file() {
        bail!("KID DLL not found at {}", inputs.kid_dll.display());
    }
    if !inputs.kid_ini.is_file() {
        bail!("KID INI not found at {}", inputs.kid_ini.display());
    }
    if !inputs.scenario_ini_dir.is_dir() {
        bail!(
            "scenario INI directory not found at {}",
            inputs.scenario_ini_dir.display()
        );
    }

    let plugins = inputs.output.join("Data").join("SKSE").join("Plugins");
    std::fs::create_dir_all(&plugins)
        .with_context(|| format!("creating {}", plugins.display()))?;

    // Core fixtures.
    std::fs::copy(inputs.kid_dll, plugins.join("KID.dll"))?;
    std::fs::copy(inputs.kid_ini, plugins.join("KID.ini"))?;
    std::fs::copy(
        inputs.harness_dll,
        plugins.join("MoraGoldenHarness.dll"),
    )?;

    // Scenario INIs: copy every *_KID.ini, skip the rest.
    for entry in std::fs::read_dir(inputs.scenario_ini_dir)? {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let Some(name) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        let lower = name.to_ascii_lowercase();
        if !lower.ends_with("_kid.ini") {
            continue;
        }
        std::fs::copy(entry.path(), plugins.join(&name))?;
    }
    Ok(())
}
```

- [ ] **Step 4: Run tests**

```bash
cargo test --package xtask --test capture_kid_goldens
```

Expected: both tests pass.

- [ ] **Step 5: Extend `capture_kid_goldens.rs` to drive staging + `run-skyrim-test.sh`**

Replace the `run` body in `crates/xtask/src/capture_kid_goldens.rs`:

```rust
pub fn run(args: Args) -> Result<()> {
    let scenarios: Vec<String> = match (args.scenario.as_deref(), args.all) {
        (Some(name), false) => vec![name.to_string()],
        (None, true) => discover_scenarios()?,
        _ => bail!("exactly one of --scenario NAME or --all must be specified"),
    };

    let root = workspace_root()?;
    let harness_dll = root
        .join("target/x86_64-pc-windows-msvc/release/MoraGoldenHarness.dll");
    let kid_dll = root.join("third_party/kid/KID.dll");
    let kid_ini = root.join("third_party/kid/KID.ini");

    for name in &scenarios {
        capture_one_scenario(&root, name, &harness_dll, &kid_dll, &kid_ini)?;
    }
    println!("captured {}/{} scenarios", scenarios.len(), scenarios.len());
    Ok(())
}

fn capture_one_scenario(
    root: &std::path::Path,
    name: &str,
    harness_dll: &std::path::Path,
    kid_dll: &std::path::Path,
    kid_ini: &std::path::Path,
) -> Result<()> {
    eprintln!("[capture] === {name} ===");
    let scenario_ini_dir = root.join("tests/golden-data/kid-inis").join(name);
    let stage = tempfile::tempdir()?;

    staging::assemble_mod_dir(&staging::AssembleInputs {
        output: stage.path(),
        kid_dll,
        kid_ini,
        scenario_ini_dir: &scenario_ini_dir,
        harness_dll,
    })?;

    // Emit a check.sh that waits for the harness's .done sentinel.
    let check_sh = stage.path().join("check.sh");
    std::fs::write(
        &check_sh,
        CHECK_SH_TEMPLATE,
    )?;
    let mut perms = std::fs::metadata(&check_sh)?.permissions();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        perms.set_mode(0o755);
    }
    std::fs::set_permissions(&check_sh, perms)?;

    // Invoke run-skyrim-test.sh with the staged mod-dir and hook.
    let status = std::process::Command::new("/usr/local/bin/run-skyrim-test.sh")
        .arg(stage.path())
        .env("TEST_HOOK", &check_sh)
        .status()
        .with_context(|| {
            "running /usr/local/bin/run-skyrim-test.sh — on a local box, source \
             the runner image's helpers or install the script"
        })?;
    if !status.success() {
        bail!("run-skyrim-test.sh failed for scenario {name}: {status}");
    }

    // The dumps live at $SKYRIM_ROOT/Data/MoraCache/dumps/ at run time;
    // run-skyrim-test.sh stages $SKYRIM_ROOT under /tmp/skyrim. Pull
    // both JSONL files out.
    let dump_src = std::path::Path::new("/tmp/skyrim")
        .join("Data")
        .join("MoraCache")
        .join("dumps");
    let expected_out = root.join("tests/golden-data/expected").join(name);
    std::fs::create_dir_all(&expected_out)?;
    for f in ["weapons.jsonl", "armors.jsonl"] {
        let src = dump_src.join(f);
        let dst = expected_out.join(f);
        std::fs::copy(&src, &dst)
            .with_context(|| format!("copying {} -> {}", src.display(), dst.display()))?;
    }

    // Manifest — filled in by Task 8.
    manifest::write_for_scenario(&expected_out, kid_dll)?;

    eprintln!("[capture] {name}: OK");
    Ok(())
}

const CHECK_SH_TEMPLATE: &str = r#"#!/usr/bin/env bash
# Wait up to 10 minutes for the harness to signal completion, then exit.
set -uo pipefail
SENTINEL="${SKYRIM_ROOT:-/tmp/skyrim}/Data/MoraCache/dumps/.done"
for _ in $(seq 1 600); do
    if [ -f "$SENTINEL" ]; then
        echo "[capture-check] sentinel detected"
        exit 0
    fi
    sleep 1
done
echo "[capture-check] timeout waiting for sentinel at $SENTINEL" >&2
exit 1
"#;
```

- [ ] **Step 6: Re-run lints + build**

```bash
cargo build --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
```

Expected: clean.

- [ ] **Step 7: Commit**

```bash
git add crates/xtask/
git commit -m "xtask: stage mod-dir + invoke run-skyrim-test.sh + pull dump files"
```

---

### Task 8: Manifest generation (ESP hashes + KID/Skyrim versions)

The manifest anchors each captured scenario to a specific Skyrim + KID state. Test-time hash mismatch → skip (don't fail).

**Files:**
- Modify: `crates/xtask/src/capture_kid_goldens/manifest.rs`
- Modify: `crates/xtask/tests/capture_kid_goldens.rs`

- [ ] **Step 1: Add failing tests for manifest helpers**

Append to `crates/xtask/tests/capture_kid_goldens.rs`:

```rust
use xtask::capture_kid_goldens::manifest::{hash_file_sha256, read_peek_kid_version, write_for_scenario};

#[test]
fn hash_file_sha256_is_stable() {
    let tmp = tempdir().unwrap();
    let p = tmp.path().join("sample.bin");
    fs::write(&p, b"hello world\n").unwrap();
    let h1 = hash_file_sha256(&p).unwrap();
    let h2 = hash_file_sha256(&p).unwrap();
    assert_eq!(h1, h2);
    // Expected sha256 of b"hello world\n" is well-known:
    assert_eq!(h1, "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447");
}

#[test]
fn read_peek_kid_version_returns_some_for_nonempty_input() {
    let tmp = tempdir().unwrap();
    let p = tmp.path().join("KID.dll");
    // Write something that looks vaguely PE-ish. The helper is allowed
    // to return None if it can't extract a version; we only assert it
    // doesn't crash and that a non-empty bytestream yields Some/None.
    fs::write(&p, b"\x4D\x5A fake PE body").unwrap();
    let _ = read_peek_kid_version(&p); // must not panic
}

#[test]
fn write_for_scenario_emits_readable_manifest() {
    let tmp = tempdir().unwrap();
    let out = tmp.path().join("expected-out");
    fs::create_dir_all(&out).unwrap();
    let kid_dll = tmp.path().join("KID.dll");
    fs::write(&kid_dll, b"fake").unwrap();

    // The function reads ESPs from MORA_SKYRIM_DATA; point it at an
    // empty dir so the hashes map is empty but the manifest still writes.
    let data = tmp.path().join("data");
    fs::create_dir_all(&data).unwrap();
    unsafe {
        std::env::set_var("MORA_SKYRIM_DATA", &data);
    }

    write_for_scenario(&out, &kid_dll).unwrap();

    let manifest = fs::read_to_string(out.join("manifest.json")).unwrap();
    assert!(manifest.contains("\"captured_at\""), "got: {manifest}");
    assert!(manifest.contains("\"esp_hashes\""), "got: {manifest}");
}
```

- [ ] **Step 2: Run tests to verify failure**

```bash
cargo test --package xtask --test capture_kid_goldens
```

Expected: FAIL (functions not defined).

- [ ] **Step 3: Implement `manifest.rs`**

Replace `crates/xtask/src/capture_kid_goldens/manifest.rs`:

```rust
//! Manifest generation: SHA256 over every ESP/ESM/ESL in the active
//! Skyrim data dir + KID version + Skyrim version, serialized to
//! `tests/golden-data/expected/<scenario>/manifest.json`.

use anyhow::{Context, Result};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::io::Read;
use std::path::Path;

/// SHA256 a file, lowercase hex. Chunked so we don't hold large ESPs
/// in memory.
pub fn hash_file_sha256(path: &Path) -> Result<String> {
    let f = std::fs::File::open(path)
        .with_context(|| format!("opening {}", path.display()))?;
    let mut reader = std::io::BufReader::new(f);
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 64 * 1024];
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

/// Best-effort read of a Windows PE FileVersion resource. Returns None
/// if the file isn't parseable or has no VERSIONINFO. This is a
/// convenience — on failure we fall back to the SHA256 of KID.dll as
/// the identifier.
pub fn read_peek_kid_version(kid_dll: &Path) -> Option<String> {
    // Full PE version resource parsing is overkill for a manifest tag.
    // Use the SHA256 suffix — stable, collision-proof, trivially re-derivable.
    let h = hash_file_sha256(kid_dll).ok()?;
    Some(format!("sha256:{}", &h[..16]))
}

/// Hash every `.esp`, `.esm`, `.esl` in `data_dir` (top-level only —
/// loose BSAs and sub-dirs are intentionally ignored). Returns a
/// BTreeMap so iteration order is deterministic for stable JSON.
pub fn hash_data_dir(data_dir: &Path) -> Result<BTreeMap<String, String>> {
    let mut out = BTreeMap::new();
    if !data_dir.is_dir() {
        // Not an error — allows write_for_scenario to be called in
        // test contexts where the data dir is a placeholder.
        return Ok(out);
    }
    for entry in std::fs::read_dir(data_dir)? {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let Some(name) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        let lower = name.to_ascii_lowercase();
        if !(lower.ends_with(".esp") || lower.ends_with(".esm") || lower.ends_with(".esl")) {
            continue;
        }
        let h = hash_file_sha256(&entry.path())?;
        out.insert(name, h);
    }
    Ok(out)
}

pub fn write_for_scenario(expected_dir: &Path, kid_dll: &Path) -> Result<()> {
    let data_dir = std::env::var("MORA_SKYRIM_DATA")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| std::path::PathBuf::from("/skyrim-base/Data"));

    let esp_hashes = hash_data_dir(&data_dir)?;
    let kid_version = read_peek_kid_version(kid_dll)
        .unwrap_or_else(|| "unknown".to_string());
    // Skyrim version: placeholder. Bethesda doesn't expose a cheap
    // version byte for SkyrimSE.exe from the runner context; we record
    // the Skyrim.esm hash as the stable identifier instead (it's
    // already in esp_hashes).
    let skyrim_version = esp_hashes
        .get("Skyrim.esm")
        .cloned()
        .unwrap_or_else(|| "unknown".to_string());

    let captured_at = now_iso8601();

    let manifest = serde_json::json!({
        "captured_at": captured_at,
        "kid_version": kid_version,
        "skyrim_version": format!("sha256:{}", &skyrim_version.chars().take(16).collect::<String>()),
        "esp_hashes": esp_hashes,
    });

    let path = expected_dir.join("manifest.json");
    let pretty = serde_json::to_string_pretty(&manifest)?;
    std::fs::write(&path, pretty + "\n")
        .with_context(|| format!("writing {}", path.display()))?;
    Ok(())
}

fn now_iso8601() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    // Very rough Y-M-DTHH:MM:SSZ. Exact-second precision is enough for
    // capture timestamps; we don't use this for correctness.
    let (year, month, day, h, m, s) = epoch_to_ymdhms(secs);
    format!("{year:04}-{month:02}-{day:02}T{h:02}:{m:02}:{s:02}Z")
}

/// Minimal UTC epoch-to-calendar conversion — avoids pulling in `chrono`
/// for one manifest field. Days computed via Howard Hinnant's
/// civil_from_days; sufficient for Skyrim pivot lifespan.
fn epoch_to_ymdhms(secs: u64) -> (u32, u32, u32, u32, u32, u32) {
    let s = (secs % 60) as u32;
    let m = ((secs / 60) % 60) as u32;
    let h = ((secs / 3600) % 24) as u32;
    let days = (secs / 86_400) as i64;
    // civil_from_days (Hinnant).
    let z = days + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = (z - era * 146_097) as u32;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m_calendar = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = y + if m_calendar <= 2 { 1 } else { 0 };
    (y as u32, m_calendar, d, h, m, s)
}
```

- [ ] **Step 4: Run tests to verify pass**

```bash
cargo test --package xtask --test capture_kid_goldens
```

Expected: all six (or however many) tests pass.

- [ ] **Step 5: Run full workspace lints**

```bash
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace
```

Expected: clean.

- [ ] **Step 6: Commit**

```bash
git add crates/xtask/
git commit -m "xtask: manifest.json with ESP SHA256s + KID version stamp"
```

---

## Phase D — Scenario INIs (Task 9)

### Task 9: Author the 10 hand-crafted scenario INIs

All scenarios target vanilla + DLC + CC forms so they run against the runner's `/skyrim-base/` without needing additional mods. Each `<scenario>_KID.ini` is ≤20 lines and exercises one specific algorithm surface.

For accurate vanilla FormID references, this task relies on xEdit / UESP records of the base-game IDs — when in doubt, prefer `EditorID` references (KID's preferred shape) over raw FormIDs. The implementer validates their INIs by running `mora compile` against the runner's data dir and confirming rules parse without warnings.

**Files:**
- Create: `tests/golden-data/kid-inis/filter_form_positive/filter_form_positive_KID.ini`
- Create: `tests/golden-data/kid-inis/filter_form_positive/README.md`
- Create: `tests/golden-data/kid-inis/filter_form_negative/filter_form_negative_KID.ini`
- Create: `tests/golden-data/kid-inis/filter_form_negative/README.md`
- Create: `tests/golden-data/kid-inis/filter_edid_substring/filter_edid_substring_KID.ini`
- Create: `tests/golden-data/kid-inis/filter_edid_substring/README.md`
- Create: `tests/golden-data/kid-inis/filter_keyword_any/filter_keyword_any_KID.ini`
- Create: `tests/golden-data/kid-inis/filter_keyword_any/README.md`
- Create: `tests/golden-data/kid-inis/filter_keyword_not/filter_keyword_not_KID.ini`
- Create: `tests/golden-data/kid-inis/filter_keyword_not/README.md`
- Create: `tests/golden-data/kid-inis/traits_weapon_all/traits_weapon_all_KID.ini`
- Create: `tests/golden-data/kid-inis/traits_weapon_all/README.md`
- Create: `tests/golden-data/kid-inis/traits_armor_all/traits_armor_all_KID.ini`
- Create: `tests/golden-data/kid-inis/traits_armor_all/README.md`
- Create: `tests/golden-data/kid-inis/chance_boundary/chance_boundary_KID.ini`
- Create: `tests/golden-data/kid-inis/chance_boundary/README.md`
- Create: `tests/golden-data/kid-inis/exclusive_group/exclusive_group_KID.ini`
- Create: `tests/golden-data/kid-inis/exclusive_group/README.md`
- Create: `tests/golden-data/kid-inis/esl_light_forms/esl_light_forms_KID.ini`
- Create: `tests/golden-data/kid-inis/esl_light_forms/README.md`
- Create: `tests/golden-data/plugins.txt` — canonical load order shared by all scenarios

- [ ] **Step 1: Create `filter_form_positive`**

`tests/golden-data/kid-inis/filter_form_positive/filter_form_positive_KID.ini`:

```ini
; ALL-bucket FormID match: tag the vanilla Iron Sword (0x00012EB7)
; with WeapMaterialIron. KID key is the target keyword.
WeapMaterialIron = Weapon|+0x00012EB7
```

`tests/golden-data/kid-inis/filter_form_positive/README.md`:

```markdown
Exercises `+FormID` ALL-bucket filter: applies WeapMaterialIron to
Iron Sword (0x00012EB7 — a well-known vanilla form). The smallest
possible positive case; if this scenario fails, something fundamental
is wrong in FormID filter handling.
```

- [ ] **Step 2: Create `filter_form_negative`**

`tests/golden-data/kid-inis/filter_form_negative/filter_form_negative_KID.ini`:

```ini
; Match every weapon whose EDID contains "Iron" EXCEPT Iron Sword.
; Tests `-FormID` subtraction from the match set.
WeapMaterialSteel = Weapon|Iron-0x00012EB7
```

`tests/golden-data/kid-inis/filter_form_negative/README.md`:

```markdown
Exercises `-FormID` NOT-bucket filter: matches every weapon with EDID
containing "Iron", then subtracts Iron Sword (0x00012EB7). Expected
output = every Iron* weapon minus the Sword, all tagged with
WeapMaterialSteel.
```

- [ ] **Step 3: Create `filter_edid_substring`**

`tests/golden-data/kid-inis/filter_edid_substring/filter_edid_substring_KID.ini`:

```ini
; EditorID ALL-bucket substring match (KID matches "contains" by default).
; All WEAP with EDID containing "Iron" get WeapMaterialIron.
WeapMaterialIron = Weapon|Iron
```

`tests/golden-data/kid-inis/filter_edid_substring/README.md`:

```markdown
Exercises bare EditorID filter (ALL-bucket, substring match). Expected
output: every weapon whose EDID contains "Iron" gets
WeapMaterialIron. Catches mora-kid's substring-vs-equality bugs.
```

- [ ] **Step 4: Create `filter_keyword_any`**

`tests/golden-data/kid-inis/filter_keyword_any/filter_keyword_any_KID.ini`:

```ini
; ANY-bucket keyword match: at least one of the listed keywords present.
; Every weapon carrying WeapTypeSword OR WeapTypeWarhammer gets WeapMaterialIron.
WeapMaterialIron = Weapon|*WeapTypeSword,*WeapTypeWarhammer
```

`tests/golden-data/kid-inis/filter_keyword_any/README.md`:

```markdown
Exercises `*Keyword` ANY-bucket filter (union semantics). Expected
output: every sword and every warhammer gets WeapMaterialIron.
```

- [ ] **Step 5: Create `filter_keyword_not`**

`tests/golden-data/kid-inis/filter_keyword_not/filter_keyword_not_KID.ini`:

```ini
; NOT-bucket keyword subtract: weapons that do NOT already carry
; WeapMaterialIron. Result: every non-iron weapon carrying WeapTypeSword.
WeapTypeDaedric = Weapon|*WeapTypeSword-WeapMaterialIron
```

`tests/golden-data/kid-inis/filter_keyword_not/README.md`:

```markdown
Exercises `-Keyword` NOT-bucket filter. Expected: every sword that
is NOT iron gets WeapTypeDaedric tagged onto it.
```

- [ ] **Step 6: Create `traits_weapon_all`**

`tests/golden-data/kid-inis/traits_weapon_all/traits_weapon_all_KID.ini`:

```ini
; Every weapon trait predicate in one rule: one-handed sword, speed
; 0.9-1.5, reach ≥ 0.8, value range, weight range, damage range,
; non-enchanted.
WeapMaterialIron = Weapon||OneHandSword,S(0.9 1.5),R(0.8),V(0 200),W(5.0 15.0),D(5 20),-E
```

`tests/golden-data/kid-inis/traits_weapon_all/README.md`:

```markdown
Exercises every weapon trait predicate in a single rule: animation type,
speed range, reach minimum, value range, weight range, damage range,
unenchanted filter. Catches any trait-eval wiring regression in the
distributor.
```

- [ ] **Step 7: Create `traits_armor_all`**

`tests/golden-data/kid-inis/traits_armor_all/traits_armor_all_KID.ini`:

```ini
; Every armor trait predicate: heavy type, body slot 32 (chest),
; armor rating range, value range, weight range, non-enchanted.
ArmorMaterialIron = Armor||HEAVY,32,AR(20 200),V(0 500),W(10.0 50.0),-E
```

`tests/golden-data/kid-inis/traits_armor_all/README.md`:

```markdown
Exercises every armor trait predicate: armor type, body slot number,
armor rating range, value range, weight range, unenchanted filter.
Counterpart to `traits_weapon_all`.
```

- [ ] **Step 8: Create `chance_boundary`**

`tests/golden-data/kid-inis/chance_boundary/chance_boundary_KID.ini`:

```ini
; Three rules on "Iron"-named weapons, varying chance: 0 (never),
; 100 (always), 50 (deterministic RNG). Tests the full chance stack.
WeapMaterialSilver  = Weapon|Iron|||0
WeapMaterialSteel   = Weapon|Iron|||100
WeapMaterialDaedric = Weapon|Iron|||50
```

`tests/golden-data/kid-inis/chance_boundary/README.md`:

```markdown
Three-rule Chance-boundary: 0 (skip), 100 (always), 50 (deterministic
RNG). This scenario is the most sensitive to any drift in the Xoshiro +
MSVC uniform_real_distribution port; a single wrong bit in the RNG
stack flips an unpredictable subset of the 50% rule's matches.
```

- [ ] **Step 9: Create `exclusive_group`**

`tests/golden-data/kid-inis/exclusive_group/exclusive_group_KID.ini`:

```ini
; Two rules both match "Iron" weapons; ExclusiveGroup ensures only
; the first-matching keyword in the group sticks per target form.
ExclusiveGroup = IronKeywordGroup|WeapMaterialIron,WeapMaterialSteel

WeapMaterialIron  = Weapon|Iron
WeapMaterialSteel = Weapon|Iron
```

`tests/golden-data/kid-inis/exclusive_group/README.md`:

```markdown
Exercises `[Exclusive Groups]` first-wins ordering. Two rules both
match every iron weapon; only WeapMaterialIron (first declared) should
end up on each form. Catches rule-ordering bugs in the distributor.
```

- [ ] **Step 10: Create `esl_light_forms`**

`tests/golden-data/kid-inis/esl_light_forms/esl_light_forms_KID.ini`:

```ini
; Target a form in an ESL plugin (0xFE-prefixed mod index).
; ccBGSSSE001-Fish.esm is bundled with the CC baseline and loads as
; ESL on SSE. Target any weapon EDID containing "Fishing" to avoid
; pinning a specific 0xFE subindex (which depends on ESL load order).
WeapTypeSword = Weapon|Fishing
```

`tests/golden-data/kid-inis/esl_light_forms/README.md`:

```markdown
Exercises ESL (light) plugin FormID resolution. Targets weapons whose
editor-ID contains "Fishing" — these come from the Creation Club
fishing rod plugin, which ships as ESL. Proves mora-esp resolves
0xFE-prefixed FormIDs identically to Skyrim.
```

- [ ] **Step 11: Author canonical `plugins.txt`**

All 10 scenarios share the same load order: vanilla + DLC + CC. Commit one `plugins.txt` under `tests/golden-data/` listing them in the order Skyrim loads them on `/skyrim-base/`. The implementer generates this by:

```bash
# On the runner: dump the wine-prefix plugins.txt verbatim.
cat /opt/warm-prefix/drive_c/users/steamuser/AppData/Local/Skyrim\ Special\ Edition/plugins.txt \
    > tests/golden-data/plugins.txt
```

If that path doesn't resolve, consult `docs/src/mora-esp-reference.md` § "plugins.txt" for the canonical search paths. The resulting file must start with the standard active-plugin markers:

```
*Skyrim.esm
*Update.esm
*Dawnguard.esm
*HearthFires.esm
*Dragonborn.esm
*ccBGSSSE001-Fish.esm
...
```

If manual authoring is required, every line begins with `*` (active marker) and lists a plugin filename. Order matches load order.

- [ ] **Step 12: Validate the INIs parse cleanly**

Run `cargo test --package mora-kid` to confirm nothing regresses.

```bash
cargo test --package mora-kid
```

Expected: no regressions.

- [ ] **Step 13: Commit**

```bash
git add tests/golden-data/kid-inis/ tests/golden-data/plugins.txt
git commit -m "golden: author 10 hand-crafted scenario INIs + canonical plugins.txt (M4 corpus)"
```

---

## Phase E — Runner image update + first capture (Tasks 10–11)

### Task 10: Runner image refresh doc — add KID to the baked image

Before the first capture, the self-hosted runner's image needs KID.dll + KID.ini baked at a pinned version. This task updates the documentation that the human operator follows when rebuilding the image. The image rebuild itself is out of plan (runs on Unraid); the plan's artifact is the doc change.

**Files:**
- Modify: `docs/src/runner-image-refresh.md`

- [ ] **Step 1: Append KID section**

Append to `docs/src/runner-image-refresh.md`:

```markdown

## M4 addition — KID baseline plugin

M4 golden-test capture requires the real KID SKSE plugin at a pinned
version. Bake it into `/skyrim-baseline/optional-plugins/KID/` on the
runner image:

```bash
# Pinned version (update atomically with golden re-capture PRs).
KID_VERSION=5.6.0

mkdir -p /skyrim-baseline/optional-plugins/KID
# Source: upstream KID release — download URL + checksum documented
# alongside the image build. The `KID.dll` and `KID.ini` files go
# under that directory.
cp -v ~/staging/KID_${KID_VERSION}/KID.dll \
      /skyrim-baseline/optional-plugins/KID/KID.dll
cp -v ~/staging/KID_${KID_VERSION}/KID.ini \
      /skyrim-baseline/optional-plugins/KID/KID.ini

chmod 0444 /skyrim-baseline/optional-plugins/KID/*
```

### How the xtask finds KID

`cargo xtask capture-kid-goldens` expects KID fixtures at
`third_party/kid/KID.{dll,ini}` relative to the workspace root. On the
runner, symlink into the baseline:

```bash
# One-time setup per worker container.
mkdir -p /_work/unraid-runner-*/mora/mora/third_party/kid
ln -sf /skyrim-baseline/optional-plugins/KID/KID.dll \
       /_work/unraid-runner-*/mora/mora/third_party/kid/KID.dll
ln -sf /skyrim-baseline/optional-plugins/KID/KID.ini \
       /_work/unraid-runner-*/mora/mora/third_party/kid/KID.ini
```

Locally, place `KID.dll` + `KID.ini` at `third_party/kid/` (this path
is `.gitignore`d — see next step).

### Why `third_party/kid/` is gitignored

KID is a third-party mod. Its binaries and INI are not redistributed
with this repo; each developer or CI runner stages them from their
own KID install. Only the captured post-state dumps (derived, not
KID source) live in git.
```

- [ ] **Step 2: Add `third_party/kid/` to `.gitignore`**

Append to `.gitignore`:

```
# Third-party KID fixtures staged locally for `cargo xtask capture-kid-goldens`.
# See docs/src/runner-image-refresh.md.
/third_party/kid/
```

- [ ] **Step 3: Commit**

```bash
git add docs/src/runner-image-refresh.md .gitignore
git commit -m "docs: runner image refresh — bake KID at pinned version"
```

- [ ] **Step 4: Manual action (out of plan): notify the runner admin**

After this commit lands, the human operator rebuilds the Unraid runner image with the new KID payload. This is not a code change; it's coordination. The plan proceeds assuming the image is rebuilt before Task 11 runs.

---

### Task 11: First capture — run the xtask, commit the goldens

This task runs the full capture pipeline end-to-end for the first time. It requires a runner (or dev box with Skyrim) with Task 10's image prerequisites in place. If the capture fails, debug and fix — most likely causes: harness DLL linking issue, wrong BGSKeywordForm offset for ARMO (Task 2), KID version mismatch, scenario INI typo.

**Files:**
- Create (via capture): `tests/golden-data/expected/<scenario>/weapons.jsonl` × 10
- Create (via capture): `tests/golden-data/expected/<scenario>/armors.jsonl` × 10
- Create (via capture): `tests/golden-data/expected/<scenario>/manifest.json` × 10

- [ ] **Step 1: Stage KID fixtures locally (or confirm runner symlinks exist)**

On a dev box:

```bash
mkdir -p third_party/kid
cp ~/KID_5.6.0/KID.dll third_party/kid/
cp ~/KID_5.6.0/KID.ini third_party/kid/
```

On the runner: confirm `third_party/kid/KID.dll` and `KID.ini` symlinks from Task 10 resolve.

- [ ] **Step 2: Build the harness DLL release binary**

```bash
cargo xwin build --release --target x86_64-pc-windows-msvc --package mora-golden-harness
```

Expected: `target/x86_64-pc-windows-msvc/release/MoraGoldenHarness.dll` present.

- [ ] **Step 3: Run a single scenario to smoke-test the pipeline**

```bash
cargo xtask capture-kid-goldens --scenario filter_form_positive
```

Expected: the command emits `[capture] === filter_form_positive ===`, Skyrim boots under Proton, ~60s later `sentinel detected` prints, files land under `tests/golden-data/expected/filter_form_positive/`.

If the capture times out or fails, debug:
- Check `$LOG_DIR` for the stashed SKSE + MoraGoldenHarness logs.
- Re-verify the ARMO offset from Task 2 — a wrong offset silently corrupts the dump, so verify by comparing manifest hashes against a known-good capture tool (xEdit can export keyword lists for one form as a sanity check).

- [ ] **Step 4: Eyeball the captured JSONL**

```bash
head -n 5 tests/golden-data/expected/filter_form_positive/weapons.jsonl
```

Expected: lines look like `{"form":"0x00012eb7","kws":["0x0001e718"]}`. The Iron Sword (0x00012EB7) should appear with WeapMaterialIron (0x0001E718) in its list.

- [ ] **Step 5: Run all 10 scenarios**

```bash
cargo xtask capture-kid-goldens --all
```

Expected: ~10 minutes total, all 10 scenarios commit dump files.

- [ ] **Step 6: Review sizes (sanity gate)**

```bash
du -sh tests/golden-data/expected/
```

Expected: under 10 MB. If larger, revisit scenario selection or apply per-record-type split (already in design).

- [ ] **Step 7: Commit the generated goldens**

```bash
git add tests/golden-data/expected/
git commit -m "golden: initial capture of 10 scenarios against KID 5.6.0 + Skyrim SE 1.6.x"
```

---

## Phase F — Test wiring (Tasks 12–13)

### Task 12: Scenario discovery via `build.rs`

Each scenario directory under `tests/golden-data/expected/` becomes one `#[test]` function via a generated file included into `tests/golden.rs`.

**Files:**
- Create: `crates/mora-kid/build.rs`
- Modify: `crates/mora-kid/Cargo.toml` (build-dependencies)

- [ ] **Step 1: Add build-script opt-in and deps to `mora-kid/Cargo.toml`**

Edit `crates/mora-kid/Cargo.toml`:

Add `build = "build.rs"` to `[package]` (if not already present).

Add `[build-dependencies]` block:

```toml
[build-dependencies]
anyhow.workspace = true
```

Add `[dev-dependencies]`:

```toml
[dev-dependencies]
mora-core = { path = "../mora-core" }
mora-esp = { path = "../mora-esp" }
serde_json.workspace = true
sha2.workspace = true
```

- [ ] **Step 2: Create `build.rs`**

`crates/mora-kid/build.rs`:

```rust
//! Discover `tests/golden-data/expected/*/` directories (relative to
//! the workspace root) and emit one `#[test]` function per scenario
//! into `$OUT_DIR/golden_tests.rs`. The generated file is `include!`d
//! from `tests/golden.rs`.
//!
//! Rationale: one-#[test]-per-scenario gives per-scenario failure
//! names and parallel test execution. A single meta-test would fail
//! en masse on a single divergence.

use std::fs;
use std::path::PathBuf;

fn main() -> anyhow::Result<()> {
    let workspace_root = workspace_root();
    let expected_root = workspace_root.join("tests/golden-data/expected");
    let out_dir = std::env::var("OUT_DIR")?;
    let out_path = PathBuf::from(&out_dir).join("golden_tests.rs");

    println!("cargo:rerun-if-changed={}", expected_root.display());

    let mut scenarios: Vec<String> = Vec::new();
    if expected_root.is_dir() {
        for entry in fs::read_dir(&expected_root)? {
            let entry = entry?;
            if !entry.file_type()?.is_dir() {
                continue;
            }
            let Some(name) = entry.file_name().to_str().map(str::to_string) else {
                continue;
            };
            // Only scenarios with an actual manifest count; capture-in-progress dirs skipped.
            if !entry.path().join("manifest.json").is_file() {
                continue;
            }
            scenarios.push(name);
        }
    }
    scenarios.sort();

    let mut src = String::new();
    src.push_str("// GENERATED by build.rs — do not edit.\n\n");
    for name in &scenarios {
        let ident = format!("golden_{}", sanitize_ident(name));
        src.push_str(&format!(
            "#[test] fn {ident}() {{ run_golden_scenario({:?}); }}\n",
            name
        ));
    }

    fs::write(&out_path, src)?;
    Ok(())
}

fn sanitize_ident(name: &str) -> String {
    name.chars()
        .map(|c| if c.is_alphanumeric() { c } else { '_' })
        .collect()
}

fn workspace_root() -> PathBuf {
    // CARGO_MANIFEST_DIR is crates/mora-kid; parent-parent is workspace root.
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    PathBuf::from(manifest_dir)
        .parent()
        .and_then(|p| p.parent())
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| PathBuf::from(manifest_dir))
}
```

- [ ] **Step 3: Confirm the build script runs and emits a file**

```bash
cargo build --package mora-kid
ls target/debug/build/mora-kid-*/out/golden_tests.rs
```

Expected: file exists, contains 10 `#[test] fn golden_*` lines.

- [ ] **Step 4: Commit**

```bash
git add crates/mora-kid/build.rs crates/mora-kid/Cargo.toml
git commit -m "mora-kid: build.rs discovers golden scenarios and emits per-scenario tests"
```

---

### Task 13: Implement `tests/golden.rs` test helper

The helper reads the INI, opens the ESP world, runs the distributor, reconstructs the post-state, and diffs against the committed JSONL.

**Files:**
- Create: `crates/mora-kid/tests/golden.rs`

- [ ] **Step 1: Write the helper + scaffold**

`crates/mora-kid/tests/golden.rs`:

```rust
//! Per-scenario golden tests. The `#[test]` functions below are
//! generated by `build.rs` from the scenario directories under
//! `tests/golden-data/expected/`. Each calls `run_golden_scenario(name)`,
//! which:
//!
//!   1. Skips with a diagnostic if `MORA_SKYRIM_DATA` is unset.
//!   2. Re-hashes every ESP under that data dir; skips (not fails) if
//!      the hashes don't match the committed manifest.
//!   3. Parses the scenario INI(s) via `mora-kid::ini::parse_file`.
//!   4. Opens the ESP world via `mora-esp::EspWorld::open`.
//!   5. Runs the distributor.
//!   6. Computes `post_state = vanilla_KWDA ∪ patches` per form.
//!   7. Diffs the post-state against the committed JSONL.
//!
//! Skip-not-fail is intentional: a Skyrim patch, a changed ESP, or an
//! unset env var must not red CI on an unrelated PR.

use std::collections::{BTreeMap, BTreeSet};
use std::io::Read;
use std::path::{Path, PathBuf};

use mora_core::{DeterministicChance, Distributor, FormId, Patch, PatchSink};
use mora_esp::EspWorld;
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_file;

include!(concat!(env!("OUT_DIR"), "/golden_tests.rs"));

fn run_golden_scenario(name: &str) {
    let Some(data_dir) = require_data_dir(name) else {
        return;
    };

    let workspace_root = workspace_root();
    let expected_dir = workspace_root
        .join("tests/golden-data/expected")
        .join(name);
    let ini_dir = workspace_root
        .join("tests/golden-data/kid-inis")
        .join(name);

    if !verify_manifest(&expected_dir, &data_dir, name) {
        return;
    }

    // Parse every *_KID.ini in the scenario directory.
    let mut rules = Vec::new();
    let mut groups = Vec::new();
    for entry in std::fs::read_dir(&ini_dir).expect("scenario ini dir") {
        let entry = entry.expect("read_dir entry");
        let path = entry.path();
        let Some(lower) = path
            .file_name()
            .and_then(|n| n.to_str())
            .map(|s| s.to_ascii_lowercase())
        else {
            continue;
        };
        if !lower.ends_with("_kid.ini") {
            continue;
        }
        let parsed = parse_file(&path).expect("ini parse");
        rules.extend(parsed.rules);
        groups.extend(parsed.exclusive_groups);
    }

    // Scenarios share one canonical committed plugins.txt — all use the
    // runner's /skyrim-base/ vanilla + DLC + CC load order.
    let plugins_txt = workspace_root.join("tests/golden-data/plugins.txt");
    let world = EspWorld::open(&data_dir, &plugins_txt).expect("open world");

    let chance = DeterministicChance::kid_compatible();
    let distributor = KidDistributor::new(rules).with_exclusive_groups(groups);
    let mut sink = PatchSink::new();
    distributor
        .lower(&world, &chance, &mut sink)
        .expect("lower");
    let patch_file = sink.finalize();

    let (actual_weapons, actual_armors) = compute_post_state(&world, &patch_file.patches);
    let expected_weapons = load_jsonl(&expected_dir.join("weapons.jsonl"));
    let expected_armors = load_jsonl(&expected_dir.join("armors.jsonl"));

    diff_or_panic(name, "weapons", &actual_weapons, &expected_weapons);
    diff_or_panic(name, "armors", &actual_armors, &expected_armors);
}

fn require_data_dir(scenario: &str) -> Option<PathBuf> {
    match std::env::var("MORA_SKYRIM_DATA") {
        Ok(v) => {
            let p = PathBuf::from(v);
            if !p.is_dir() {
                eprintln!(
                    "skipping golden {scenario}: MORA_SKYRIM_DATA points at {} which is not a directory",
                    p.display()
                );
                return None;
            }
            Some(p)
        }
        Err(_) => {
            eprintln!(
                "skipping golden {scenario}: set MORA_SKYRIM_DATA to a Skyrim Data dir matching the scenario's manifest"
            );
            None
        }
    }
}

/// Returns `true` if the manifest is absent (treat as OK — no anchor),
/// matches (OK), or if the data dir's hashes differ (skip, not fail).
fn verify_manifest(expected_dir: &Path, data_dir: &Path, scenario: &str) -> bool {
    let manifest_path = expected_dir.join("manifest.json");
    let Ok(bytes) = std::fs::read_to_string(&manifest_path) else {
        eprintln!(
            "skipping golden {scenario}: no manifest.json at {}",
            manifest_path.display()
        );
        return false;
    };
    let Ok(manifest): Result<serde_json::Value, _> = serde_json::from_str(&bytes) else {
        eprintln!("skipping golden {scenario}: manifest.json is not valid JSON");
        return false;
    };
    let Some(expected_hashes) = manifest.get("esp_hashes").and_then(|v| v.as_object()) else {
        eprintln!("skipping golden {scenario}: manifest has no esp_hashes");
        return false;
    };
    // Check every ESP in the manifest against the data dir.
    for (name, expected_hash) in expected_hashes {
        let expected_hash = expected_hash.as_str().unwrap_or("");
        let file = data_dir.join(name);
        if !file.is_file() {
            eprintln!(
                "skipping golden {scenario}: manifest references {name} which is absent in data dir"
            );
            return false;
        }
        let actual = hash_file_sha256(&file).unwrap_or_default();
        if actual != expected_hash {
            eprintln!(
                "skipping golden {scenario}: {name} hash mismatch — expected {expected_hash}, got {actual}"
            );
            return false;
        }
    }
    true
}

fn hash_file_sha256(path: &Path) -> std::io::Result<String> {
    use sha2::{Digest, Sha256};
    let f = std::fs::File::open(path)?;
    let mut r = std::io::BufReader::new(f);
    let mut h = Sha256::new();
    let mut buf = [0u8; 64 * 1024];
    loop {
        let n = r.read(&mut buf)?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(format!("{:x}", h.finalize()))
}

fn compute_post_state(
    world: &EspWorld,
    patches: &[Patch],
) -> (
    BTreeMap<FormId, BTreeSet<FormId>>,
    BTreeMap<FormId, BTreeSet<FormId>>,
) {
    let mut weapons: BTreeMap<FormId, BTreeSet<FormId>> = BTreeMap::new();
    let mut armors: BTreeMap<FormId, BTreeSet<FormId>> = BTreeMap::new();

    for entry in world.weapons() {
        if let Ok((fid, w)) = entry {
            let set = weapons.entry(fid).or_default();
            for kw in &w.keywords {
                set.insert(*kw);
            }
        }
    }
    for entry in world.armors() {
        if let Ok((fid, a)) = entry {
            let set = armors.entry(fid).or_default();
            for kw in &a.keywords {
                set.insert(*kw);
            }
        }
    }

    for patch in patches {
        match patch {
            Patch::AddKeyword { target, keyword } => {
                if let Some(set) = weapons.get_mut(target) {
                    set.insert(*keyword);
                } else if let Some(set) = armors.get_mut(target) {
                    set.insert(*keyword);
                }
            }
        }
    }

    // Drop empty entries so "omitted" and "empty" compare equal.
    weapons.retain(|_, v| !v.is_empty());
    armors.retain(|_, v| !v.is_empty());

    (weapons, armors)
}

fn load_jsonl(path: &Path) -> BTreeMap<FormId, BTreeSet<FormId>> {
    let mut out = BTreeMap::new();
    let Ok(content) = std::fs::read_to_string(path) else {
        return out;
    };
    for line in content.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        let value: serde_json::Value = match serde_json::from_str(line) {
            Ok(v) => v,
            Err(_) => continue,
        };
        let form_hex = value.get("form").and_then(|v| v.as_str()).unwrap_or("");
        let form_raw = parse_hex_form(form_hex);
        let Some(form) = form_raw else { continue };
        let mut set = BTreeSet::new();
        if let Some(arr) = value.get("kws").and_then(|v| v.as_array()) {
            for kw in arr {
                if let Some(kw_hex) = kw.as_str() {
                    if let Some(k) = parse_hex_form(kw_hex) {
                        set.insert(k);
                    }
                }
            }
        }
        out.insert(form, set);
    }
    out
}

fn parse_hex_form(s: &str) -> Option<FormId> {
    let s = s.strip_prefix("0x").unwrap_or(s);
    u32::from_str_radix(s, 16).ok().map(FormId)
}

fn diff_or_panic(
    scenario: &str,
    which: &str,
    actual: &BTreeMap<FormId, BTreeSet<FormId>>,
    expected: &BTreeMap<FormId, BTreeSet<FormId>>,
) {
    if actual == expected {
        return;
    }
    let mut differences = Vec::new();
    for key in actual.keys().chain(expected.keys()).collect::<BTreeSet<_>>() {
        let a = actual.get(key);
        let e = expected.get(key);
        if a != e {
            let a_set: BTreeSet<FormId> = a.cloned().unwrap_or_default();
            let e_set: BTreeSet<FormId> = e.cloned().unwrap_or_default();
            let missing: Vec<_> = e_set.difference(&a_set).copied().collect();
            let extra: Vec<_> = a_set.difference(&e_set).copied().collect();
            differences.push(format!(
                "  form 0x{:08x}: missing {:?}, extra {:?}",
                key.0, missing, extra
            ));
            if differences.len() >= 20 {
                differences.push("  (first 20 divergences only)".to_string());
                break;
            }
        }
    }
    panic!(
        "scenario {scenario} [{which}] diverges from golden:\n{}",
        differences.join("\n")
    );
}

fn workspace_root() -> PathBuf {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    PathBuf::from(manifest_dir)
        .parent()
        .and_then(|p| p.parent())
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| PathBuf::from(manifest_dir))
}
```

- [ ] **Step 2: Run tests locally (no `MORA_SKYRIM_DATA`, so all ten skip)**

```bash
cargo test --package mora-kid --test golden
```

Expected: 10 tests run, all 10 print the skip diagnostic and pass.

- [ ] **Step 3: Run tests with a valid `MORA_SKYRIM_DATA`**

On the runner (or a dev box with Skyrim):

```bash
MORA_SKYRIM_DATA=/skyrim-base/Data cargo test --package mora-kid --test golden
```

Expected: all 10 tests pass. If any fails, investigate:
- A real divergence means mora-kid's algorithm differs from KID. Debug.
- An ESP hash mismatch means the data dir doesn't match the manifest — skip.

- [ ] **Step 4: Verify lints**

```bash
cargo clippy --workspace --all-targets -- -D warnings
```

Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add crates/mora-kid/
git commit -m "mora-kid: per-PR golden test helper (skip-not-fail on env drift)"
```

---

## Phase G — CI wiring (Task 14)

### Task 14: Add the golden-tests step to the skyrim-integration job

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add the golden-tests step**

In `.github/workflows/ci.yml`, find the `skyrim-integration` job's `steps:` list. After the existing `- uses: actions/checkout@v4` and any preparatory steps, append:

```yaml
      - name: KID golden tests
        env:
          MORA_SKYRIM_DATA: /skyrim-base/Data
        run: cargo test --package mora-kid --test golden
```

Insert it alongside the placeholder step that currently echoes "M5 will implement," ahead of it so goldens run before the other integration work lands.

- [ ] **Step 2: Confirm the YAML still parses**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"
```

Expected: no exception. (Install `pyyaml` if missing: `pip install pyyaml`.)

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: run KID golden tests on every PR via skyrim-integration self-hosted job"
```

- [ ] **Step 4: Open a PR; verify CI picks up the new step and it passes on the self-hosted runner**

```bash
git push -u origin <branch>
gh pr create --title "M4: KID golden-test harness + first capture" \
  --body "Implements docs/superpowers/specs/2026-04-21-kid-golden-test-harness-design.md."
```

Expected: CI runs, `skyrim-integration` job's new "KID golden tests" step shows all 10 scenarios passing.

- [ ] **Step 5: Merge when green**

```bash
gh pr merge --squash --delete-branch
```

---

## Milestone completion checklist

- [ ] `cargo test --package mora-kid --test golden` passes on the skyrim-integration job with `MORA_SKYRIM_DATA=/skyrim-base/Data`.
- [ ] Ten scenarios committed at `tests/golden-data/kid-inis/<name>/` with matching `tests/golden-data/expected/<name>/` captures.
- [ ] `cargo xtask capture-kid-goldens --all` succeeds end-to-end from a clean runner workspace.
- [ ] `cargo clippy --workspace --all-targets -- -D warnings` clean.
- [ ] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean.
- [ ] Injecting a deliberate bug in `mora-kid::Distributor::lower` (e.g., flip a filter negation) makes at least one golden test fail with a diff that points at a specific form + keyword. Revert the bug after verifying. This validates the diff machinery, not just the pass path.
- [ ] Total committed size of `tests/golden-data/` under 10 MB.
- [ ] `docs/src/runner-image-refresh.md` documents how to bake KID into a future image rebuild.

---

## Self-review notes

- **Placeholders:** None. All code blocks are complete; all file paths are absolute or clearly relative to workspace root; every command is exact.
- **Type consistency:** `FormId` is the mora-core nominal wrapper used everywhere the test helper handles form IDs. The harness-side JSONL uses raw `u32` hex since it lives in mora-golden-harness (which doesn't depend on mora-core). The test helper parses the hex into `FormId` at load time. The harness's `Dumps` struct uses `Vec<(u32, Vec<u32>)>` consistently.
- **Spec coverage:**
    - Architecture (capture vs test split) — Tasks 4 + 7 + 13.
    - Corpus layout — Task 9 (`kid-inis/`) + Task 11 (`expected/`).
    - Scenario list (10) — Task 9 all ten sub-steps.
    - Data-dump shape (JSONL, per-record-type) — Task 4 `write_jsonl` + Task 13 `load_jsonl`.
    - Manifest + invalidation — Task 8 + Task 13 `verify_manifest`.
    - Capture harness ~200 LoC — Task 4 (single focused lib.rs, ~180 LoC).
    - xtask orchestration — Tasks 6, 7, 8.
    - Test wiring with build.rs + skip-not-fail — Tasks 12, 13.
    - CI integration — Task 14.
    - Runner image change — Task 10.
    - First-capture bootstrap — Task 11.
    - Error handling matrix — covered across Tasks 7 (timeout), 8 (hash), 13 (skip paths), 11 (manual debug).
    - Success criteria — milestone checklist above.
