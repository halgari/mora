# Rust + KID Pivot — Plan 2: `skse-rs` Foundation (M1, Part 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a Rust SKSE plugin that loads cleanly into Skyrim SE/AE via the SKSE loader, announces itself via the plugin-info protocol, writes a message to a log file, and exits gracefully. No game-memory interaction, no Address Library. The deliverable is an end-to-end proof that a pure-Rust DLL is accepted by SKSE, runs, and can be observed to run.

**Architecture:** Three C-ABI exports (`SKSEPlugin_Version` constinit data, `SKSEPlugin_Query` function, `SKSEPlugin_Load` function) are defined in the `skse-rs` crate. A smoke-test sibling crate (`skse-rs-smoke` under `crates/skse-rs/examples/` or a dedicated stub) consumes `skse-rs` and produces a loadable `.dll`. File-based logging goes to the standard Skyrim My Games path. No reverse-engineering in this plan — all needed data (plugin info layout, messaging IDs) comes from the approved research report in the brainstorm history and is transcribed directly into Rust types.

**Tech Stack:** Rust 1.90 (workspace pin), `windows-sys` crate for Win32 APIs (HeapAlloc / GetProcessHeap / GetModuleHandleW / known-folder resolution / write file), `widestring` for UTF-16 conversion. Cross-compiled to `x86_64-pc-windows-msvc` via `cargo-xwin`. Loaded into Skyrim under Proton via the existing self-hosted runner pipeline for validation.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope note:** This plan implements the non-game portion of spec milestone M1. The game-interaction portion (Address Library, TESDataHandler, AddKeyword) is **Plan 3**, written after this plan lands. Splitting keeps each plan reviewable and each PR focused.

---

## File Structure

**Created:**
- `crates/skse-rs/src/lib.rs` — crate root; re-exports module API
- `crates/skse-rs/src/ffi.rs` — all `#[repr(C)]` SKSE ABI types + C-ABI entry points (plugin exports)
- `crates/skse-rs/src/version.rs` — `PluginVersion`, `RuntimeVersion`, packing helpers
- `crates/skse-rs/src/plugin.rs` — idiomatic Rust interface downstream crates implement (`SksePlugin` trait, `declare_plugin!` macro)
- `crates/skse-rs/src/log.rs` — file-based logger writing to the SKSE log directory
- `crates/skse-rs/src/lib_tests.rs` — unit tests for layouts and packing (in `#[cfg(test)]` module)
- `crates/skse-rs/tests/layout.rs` — integration tests for ABI struct sizes
- `crates/skse-rs-smoke/Cargo.toml` — new workspace crate; cdylib
- `crates/skse-rs-smoke/src/lib.rs` — smallest useful plugin that uses `skse-rs`
- `docs/src/skse-rs-ffi-reference.md` — internal reference doc: what each ABI type maps to, and where it came from in CommonLibSSE-NG

**Modified:**
- `Cargo.toml` (workspace root) — add `crates/skse-rs-smoke` to members
- `crates/skse-rs/Cargo.toml` — add Windows dependencies (target-gated), declare `dev-dependencies` for tests
- `crates/skse-rs/src/lib.rs` — replace current `//!` stub with the real module layout
- `docs/src/architecture.md` — note the new `skse-rs-smoke` crate under M1

**Not modified:**
- `.github/workflows/ci.yml` — fmt/clippy/test/windows-cross already cover the new code; `skyrim-integration` stays `if: false` until Plan 3 adds a form-lookup smoke test worth running in real Skyrim.

---

## Phase A — Research Resolution (Task 1)

This plan begins with a small research task to lock in the two remaining uncertainties from the design pass. The outputs land as committed reference material, not code — so the whole rest of the plan proceeds from concrete facts.

### Task 1: Verify SKSE ABI struct sizes and commit the FFI reference doc

**Files:**
- Create: `docs/src/skse-rs-ffi-reference.md`

Plan 2 depends on four SKSE ABI types being laid out exactly. If any size is wrong, the plugin silently fails to load (SKSE rejects plugins whose `SKSEPlugin_Version` doesn't match the expected size). The research report lists all four; this task transcribes them into a committed reference doc that later tasks cite directly.

- [ ] **Step 1: Write the FFI reference doc**

Run:
```bash
cat > docs/src/skse-rs-ffi-reference.md <<'EOF'
# skse-rs ABI Reference

This doc is the source of truth for the `#[repr(C)]` layouts in
`skse-rs`. Every struct here has a citation to its origin in
CommonLibSSE-NG (the C++ SKSE framework skse-rs intentionally does
*not* depend on — we mirror layouts from CommonLibSSE-NG's public
headers to stay wire-compatible with SKSE).

All offsets are in bytes from the struct start. All fields are
little-endian. Total sizes are asserted at compile time via
`const _: () = assert!(size_of::<T>() == N);`.

## `PluginVersionData` — the plugin-info data export

Data export symbol: `SKSEPlugin_Version`.
Source: `CommonLibSSE-NG/include/SKSE/Interfaces.h` (the
`SKSEPluginInfo` macro region, `static_assert(sizeof(PluginVersionData) == 0x350)`).

Total size: `0x350` bytes.

| Offset | Size | Field name            | Type          | Meaning                                                                |
|--------|------|-----------------------|---------------|------------------------------------------------------------------------|
| 0x000  | 4    | `data_version`        | `u32`         | must equal `1`                                                         |
| 0x004  | 4    | `plugin_version`      | `u32`         | packed `(major<<24)\|(minor<<16)\|(patch<<4)\|build` per `REL::Version` |
| 0x008  | 256  | `plugin_name`         | `[u8; 256]`   | NUL-terminated UTF-8 plugin identifier                                 |
| 0x108  | 256  | `author`              | `[u8; 256]`   | NUL-terminated                                                         |
| 0x208  | 252  | `support_email`       | `[u8; 252]`   | NUL-terminated                                                         |
| 0x304  | 4    | flags group A         | `u32`         | bit 0 = `no_struct_use`                                                |
| 0x308  | 4    | flags group B         | `u32`         | bit 0 = `address_library`, bit 1 = `sig_scanning`, bit 2 = `structs_post629` |
| 0x30C  | 64   | `compatible_versions` | `[u32; 16]`   | packed runtime versions; zero-terminated list                          |
| 0x34C  | 4    | `xse_minimum`         | `u32`         | minimum SKSE version; zero = any                                       |

**Mora's defaults for v0.1:**
- `data_version = 1`
- `plugin_name = "MoraRuntime"` (or `"MoraTestHarness"` for the harness build; `skse-rs-smoke` uses `"SkseRsSmoke"`)
- flags group B bit 0 (`address_library`) = `1` — so compatibility is checked against the address library rather than an exact runtime match
- `compatible_versions = [0, 0, ...]` — an all-zero list means "any version (address library handles compatibility)"

## `PluginInfo` — the info struct passed to `SKSEPlugin_Query`

Source: `CommonLibSSE-NG/include/SKSE/Interfaces.h`, `SKSEPlugin_Query` signature.

Total size: `0x18` bytes (24). Breakdown:

- `info_version` at offset `0x00` (4 bytes)
- 4 bytes of natural padding so the pointer is 8-byte aligned
- `name` at offset `0x08` (8 bytes)
- `version` at offset `0x10` (4 bytes)
- 4 bytes of trailing padding so the struct itself is 8-byte aligned (for arrays)
- Total: 4 + 4 + 8 + 4 + 4 = 24 = `0x18`

| Offset | Size | Field          | Type            | Meaning                                     |
|--------|------|----------------|-----------------|---------------------------------------------|
| 0x00   | 4    | `info_version` | `u32`           | must be `1`                                 |
| 0x08   | 8    | `name`         | `*const c_char` | pointer to a static NUL-terminated string   |
| 0x10   | 4    | `version`      | `u32`           | packed plugin version (same as above)       |

## `SKSEInterface` — the first argument to `SKSEPlugin_Load`

Source: `CommonLibSSE-NG/include/SKSE/Impl/Stubs.h`.

Total size (function pointers on x64 are 8 bytes each, `u32` fields are 4 bytes with 4 bytes of padding before the first pointer):

| Offset | Size | Field                 | Type              |
|--------|------|-----------------------|-------------------|
| 0x00   | 4    | `skse_version`        | `u32`             |
| 0x04   | 4    | `runtime_version`     | `u32`             |
| 0x08   | 4    | `editor_version`      | `u32`             |
| 0x0C   | 4    | `is_editor`           | `u32` (bool as u32) |
| 0x10   | 8    | `query_interface`     | fn(u32) -> *mut c_void |
| 0x18   | 8    | `get_plugin_handle`   | fn() -> u32       |
| 0x20   | 8    | `get_release_index`   | fn() -> u32       |
| 0x28   | 8    | `get_plugin_info`     | fn(*const c_char) -> *const c_void |

Total: `0x30` bytes.

## `SKSEMessagingInterface` — queried via `query_interface(5)` at load time

Source: `CommonLibSSE-NG/include/SKSE/Impl/Stubs.h`.

`kMessaging` interface ID: `5` (from `LoadInterface` enum in `Interfaces.h`).

| Offset | Size | Field                    | Type |
|--------|------|--------------------------|------|
| 0x00   | 4    | `interface_version`      | `u32` (== 2 at AE) |
| 0x08   | 8    | `register_listener`      | fn(handle: u32, sender: *const c_char, callback: *mut c_void) -> bool |
| 0x10   | 8    | `dispatch`               | fn(handle: u32, msg_type: u32, data: *mut c_void, data_len: u32, receiver: *const c_char) -> bool |
| 0x18   | 8    | `get_event_dispatcher`   | fn(id: u32) -> *mut c_void |

Total: `0x20` bytes.

## Message types

From `CommonLibSSE-NG/include/SKSE/Interfaces.h`, `MessagingInterface::kXxx`:

| Name             | Value |
|------------------|-------|
| `kPostLoad`      | 0     |
| `kPostPostLoad`  | 1     |
| `kPreLoadGame`   | 2     |
| `kPostLoadGame`  | 3     |
| `kSaveGame`      | 4     |
| `kDeleteGame`    | 5     |
| `kInputLoaded`   | 6     |
| `kNewGame`       | 7     |
| `kDataLoaded`    | 8     |

Mora's runtime registers for **`kDataLoaded` (8)**.

## Version packing (`REL::Version`)

Source: `CommonLibSSE-NG/include/REL/Version.h`.

```
packed = ((major & 0xFF) << 24)
       | ((minor & 0xFF) << 16)
       | ((patch & 0xFFF) << 4)
       | (build & 0xF)
```

Plugin version example: Mora v0.1.0 → `(0 << 24) | (1 << 16) | (0 << 4) | 0 = 0x0001_0000`.

Runtime version example: Skyrim SE 1.6.1170 → `(1 << 24) | (6 << 16) | (1170 << 4) | 0 = 0x0106_4920`.

For Mora we don't compare runtime versions directly — we set `address_library = true` and `compatible_versions = [0]`, which signals to SKSE: "load me on any version; I use the Address Library to resolve my own offsets." Plan 3 implements the Address Library parser.
EOF
```

- [ ] **Step 2: Commit**

Run:
```bash
git add docs/src/skse-rs-ffi-reference.md
git commit -m "docs: add skse-rs ABI reference

Source of truth for the #[repr(C)] layouts in skse-rs. Each struct
cites its origin in CommonLibSSE-NG; offsets and sizes are documented
so layout unit tests have something to validate against."
```

- [ ] **Step 3: Self-review**

```bash
# Confirm file exists and is valid markdown
test -f docs/src/skse-rs-ffi-reference.md
# Confirm all four struct tables are present
grep -c '^## `' docs/src/skse-rs-ffi-reference.md  # expect: 4
# Confirm the commit landed
git log --oneline -1
```

---

## Phase B — Crate Shape & Windows Dependencies (Tasks 2-3)

### Task 2: Add Windows dependencies to `skse-rs` and introduce the module layout

**Files:**
- Modify: `crates/skse-rs/Cargo.toml`
- Modify: `crates/skse-rs/src/lib.rs`
- Create: `crates/skse-rs/src/ffi.rs`
- Create: `crates/skse-rs/src/version.rs`
- Create: `crates/skse-rs/src/plugin.rs`
- Create: `crates/skse-rs/src/log.rs`

- [ ] **Step 1: Update `crates/skse-rs/Cargo.toml` with Windows deps**

Run:
```bash
cat > crates/skse-rs/Cargo.toml <<'EOF'
[package]
name = "skse-rs"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "Clean-room Rust SKSE plugin framework for Skyrim SE/AE. Not a binding to CommonLibSSE-NG."
readme = "README.md"
keywords = ["skyrim", "skse", "modding"]
categories = ["external-ffi-bindings", "game-development"]

[lib]
# rlib by default; downstream crates that need a cdylib for Skyrim build their own.

[dependencies]
thiserror.workspace = true
tracing.workspace = true

[target.'cfg(windows)'.dependencies]
windows-sys = { version = "0.60", features = [
    "Win32_Foundation",
    "Win32_System_LibraryLoader",
    "Win32_System_Memory",
    "Win32_Storage_FileSystem",
    "Win32_UI_Shell",
    "Win32_UI_Shell_Common",
] }
widestring = "1"
EOF
```

Rationale:
- `windows-sys` is the FFI-only binding (no runtime overhead, unlike `windows`). Feature selection gives us: `GetProcessHeap` / `HeapAlloc` (for Plan 3, but pulled now so the crate has a stable dep set), `GetModuleHandleW` (image base lookup in Plan 3), `SHGetKnownFolderPath` (for the log directory).
- `widestring` gives us easy UTF-16 handling for Windows path APIs.
- Both deps are target-gated: on non-Windows (Linux CI), they don't compile. The crate still has meaningful content on Linux (the layout unit tests run cross-platform).

- [ ] **Step 2: Rewrite `crates/skse-rs/src/lib.rs` as the module root**

Run:
```bash
cat > crates/skse-rs/src/lib.rs <<'EOF'
//! `skse-rs` — Rust-native SKSE plugin framework.
//!
//! Clean-room Rust port of the SKSE plugin infrastructure Mora needs.
//! Not a binding to the C++ CommonLibSSE-NG library; every ABI type
//! is defined directly in Rust with layout validated by compile-time
//! size asserts.
//!
//! See `docs/src/skse-rs-ffi-reference.md` for the source-of-truth
//! layout reference.
//!
//! This crate is no-std-friendly in principle but currently relies on
//! `std` for logging file I/O.
//!
//! # M1 Foundation scope
//!
//! Plan 2 (this milestone's first half) implements:
//! - ABI types (`PluginVersionData`, `PluginInfo`, `SKSEInterface`,
//!   `SKSEMessagingInterface`, messaging IDs).
//! - The three plugin exports (`SKSEPlugin_Version`,
//!   `SKSEPlugin_Query`, `SKSEPlugin_Load`).
//! - File-based logger writing to the Skyrim log directory.
//! - `SksePlugin` trait + `declare_plugin!` macro for downstream
//!   crates to opt in cleanly.
//!
//! Plan 3 adds: Address Library parser, relocation layer, game type
//! layouts, `TESDataHandler` form lookup, `AddKeyword` re-implementation.

pub mod ffi;
pub mod log;
pub mod plugin;
pub mod version;

pub use plugin::{declare_plugin, LoadError, LoadOutcome, SksePlugin};
pub use version::{PluginVersion, RuntimeVersion};
EOF
```

- [ ] **Step 3: Create empty module stubs for `ffi.rs`, `version.rs`, `plugin.rs`, `log.rs`**

Each module will be filled in by subsequent tasks. Creating them as stubs now keeps `cargo check` green at the end of this task.

Run:
```bash
cat > crates/skse-rs/src/ffi.rs <<'EOF'
//! SKSE C-ABI types and plugin entry points. Populated in Phase C.
EOF

cat > crates/skse-rs/src/version.rs <<'EOF'
//! Version packing for plugins and Skyrim runtime. Populated in Phase C.
EOF

cat > crates/skse-rs/src/plugin.rs <<'EOF'
//! Idiomatic `SksePlugin` trait + `declare_plugin!` macro. Populated in Phase D.

/// Placeholder — real impl lands in Phase D.
pub trait SksePlugin {}

/// Placeholder — real impl lands in Phase D.
#[derive(Debug)]
pub struct LoadError;

/// Placeholder — real impl lands in Phase D.
pub type LoadOutcome = Result<(), LoadError>;

/// Placeholder — real impl lands in Phase D.
#[macro_export]
macro_rules! declare_plugin {
    () => {};
}
EOF

cat > crates/skse-rs/src/log.rs <<'EOF'
//! File-based logger for SKSE plugins. Populated in Phase E.
EOF
```

The placeholder `SksePlugin` / `LoadError` / `LoadOutcome` / `declare_plugin!` are exported so that the re-exports in `lib.rs` don't fail. Phase D replaces all of them with the real trait, error type, and macro.

- [ ] **Step 4: Verify build**

```bash
cargo check --package skse-rs --all-targets
cargo check --package skse-rs --all-targets --target x86_64-pc-windows-msvc 2>/dev/null || cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Expected: both commands succeed. No warnings about unused deps (they're target-gated; unused on Linux is expected with no warning).

- [ ] **Step 5: Commit**

```bash
git add crates/skse-rs/Cargo.toml crates/skse-rs/src/
git commit -m "skse-rs: set up module layout and Windows dependencies

Crate restructured around four modules (ffi/version/plugin/log) that
later tasks fill in. Windows-only deps (windows-sys features for
LibraryLoader/Memory/Shell, widestring) gated to target cfg(windows).
All modules are stub-populated so cargo check stays green."
```

---

### Task 3: Create the `skse-rs-smoke` companion crate (empty shell)

**Files:**
- Create: `crates/skse-rs-smoke/Cargo.toml`
- Create: `crates/skse-rs-smoke/src/lib.rs`
- Modify: `Cargo.toml` (workspace root) — add new member

- [ ] **Step 1: Add the member to the workspace root**

Run:
```bash
python3 - <<'PY'
from pathlib import Path
p = Path("Cargo.toml")
text = p.read_text()
# Insert "crates/skse-rs-smoke" after "crates/skse-rs"
marker = '    "crates/skse-rs",\n'
assert marker in text, "expected workspace member line not found"
new_line = '    "crates/skse-rs-smoke",\n'
text = text.replace(marker, marker + new_line, 1)
p.write_text(text)
PY
```

Verify:
```bash
grep -n 'crates/skse-rs' Cargo.toml
```
Expected:
```
    "crates/skse-rs",
    "crates/skse-rs-smoke",
```

- [ ] **Step 2: Create `crates/skse-rs-smoke/Cargo.toml`**

```bash
mkdir -p crates/skse-rs-smoke/src
cat > crates/skse-rs-smoke/Cargo.toml <<'EOF'
[package]
name = "skse-rs-smoke"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "Smoke-test SKSE plugin built on skse-rs. Loads, logs, exits. No game interaction."
publish = false

[lib]
name = "SkseRsSmoke"
crate-type = ["cdylib"]

[dependencies]
skse-rs = { path = "../skse-rs" }
EOF
```

- [ ] **Step 3: Create a stub `crates/skse-rs-smoke/src/lib.rs`**

```bash
cat > crates/skse-rs-smoke/src/lib.rs <<'EOF'
//! Smoke-test plugin for `skse-rs`.
//!
//! The smallest useful SKSE plugin: loads successfully, writes one
//! line to its log file, returns. Exercises everything in `skse-rs`
//! Plan 2 end-to-end.
//!
//! Populated in Phase E/F of Plan 2.

// The cdylib output must be named SkseRsSmoke (SKSE plugin convention).
#![allow(non_snake_case)]
EOF
```

- [ ] **Step 4: Verify workspace still builds**

```bash
cargo check --workspace --all-targets
```
Expected: clean, 9 crates (the new `skse-rs-smoke` added to 8 existing).

- [ ] **Step 5: Commit**

```bash
git add Cargo.toml crates/skse-rs-smoke
git commit -m "skse-rs-smoke: add smoke-test companion crate stub

Empty cdylib that will consume skse-rs end-to-end. Real plugin code
lands in Phase E/F. Crate type cdylib + lib name SkseRsSmoke produce
SkseRsSmoke.dll when cross-compiled."
```

---

## Phase C — ABI Types & Version Packing (Tasks 4-6)

Everything in this phase is data — no function calls, no I/O. Unit-testable end-to-end with compile-time size asserts and property-based round-trips.

### Task 4: Implement `PluginVersion` and `RuntimeVersion` packing

**Files:**
- Modify: `crates/skse-rs/src/version.rs`
- Test: inline `#[cfg(test)] mod tests` inside `version.rs`

- [ ] **Step 1: Write the failing test**

```bash
cat > crates/skse-rs/src/version.rs <<'EOF'
//! Version packing for plugins and Skyrim runtime.
//!
//! Matches `REL::Version` in CommonLibSSE-NG:
//! packed = (major & 0xFF) << 24 | (minor & 0xFF) << 16 | (patch & 0xFFF) << 4 | (build & 0xF)

/// A plugin's own version — embedded in `SKSEPlugin_Version` data.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PluginVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u16, // 12-bit-effective, asserted <= 0xFFF in `pack`
    pub build: u8,  // 4-bit-effective, asserted <= 0xF in `pack`
}

impl PluginVersion {
    /// Pack this version into the 32-bit layout expected by SKSE.
    pub const fn pack(self) -> u32 {
        assert!(self.patch <= 0xFFF, "patch exceeds 12 bits");
        assert!(self.build <= 0xF, "build exceeds 4 bits");
        ((self.major as u32) << 24)
            | ((self.minor as u32) << 16)
            | ((self.patch as u32) << 4)
            | (self.build as u32)
    }
}

/// A Skyrim runtime version — used for `compatible_versions` matching.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u16,
    pub build: u8,
}

impl RuntimeVersion {
    /// Same layout as `PluginVersion::pack`.
    pub const fn pack(self) -> u32 {
        assert!(self.patch <= 0xFFF, "patch exceeds 12 bits");
        assert!(self.build <= 0xF, "build exceeds 4 bits");
        ((self.major as u32) << 24)
            | ((self.minor as u32) << 16)
            | ((self.patch as u32) << 4)
            | (self.build as u32)
    }

    /// Well-known runtime: Skyrim SE 1.6.1170 (current AE).
    pub const SE_1_6_1170: RuntimeVersion = RuntimeVersion {
        major: 1,
        minor: 6,
        patch: 1170,
        build: 0,
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plugin_version_packs_to_expected_bits() {
        // Mora v0.1.0 → (0,1,0,0) → 0x0001_0000
        let v = PluginVersion {
            major: 0,
            minor: 1,
            patch: 0,
            build: 0,
        };
        assert_eq!(v.pack(), 0x0001_0000);
    }

    #[test]
    fn runtime_version_ae_1_6_1170_packs_to_expected_bits() {
        // 1.6.1170.0 → (1,6,1170,0) → 0x0106_4920
        // Verify: 1170 = 0x492, shifted left 4 = 0x4920. OR with 1<<24 = 0x01000000,
        //         6<<16 = 0x00060000 → 0x01064920.
        assert_eq!(RuntimeVersion::SE_1_6_1170.pack(), 0x0106_4920);
    }

    #[test]
    fn plugin_version_max_field_values_pack_correctly() {
        let v = PluginVersion {
            major: 0xFF,
            minor: 0xFF,
            patch: 0xFFF,
            build: 0xF,
        };
        assert_eq!(v.pack(), 0xFFFF_FFFF);
    }

    #[test]
    #[should_panic(expected = "patch exceeds 12 bits")]
    fn plugin_version_rejects_overflowing_patch() {
        let v = PluginVersion {
            major: 0,
            minor: 0,
            patch: 0x1000, // one too many
            build: 0,
        };
        let _ = v.pack();
    }

    #[test]
    #[should_panic(expected = "build exceeds 4 bits")]
    fn plugin_version_rejects_overflowing_build() {
        let v = PluginVersion {
            major: 0,
            minor: 0,
            patch: 0,
            build: 0x10,
        };
        let _ = v.pack();
    }
}
EOF
```

- [ ] **Step 2: Run the tests**

```bash
cargo test --package skse-rs --lib version::tests
```

Expected: 5 tests pass.

- [ ] **Step 3: Run on Windows target too**

```bash
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add crates/skse-rs/src/version.rs
git commit -m "skse-rs: PluginVersion + RuntimeVersion packing

Implements REL::Version bit layout (major<<24 | minor<<16 | patch<<4 |
build) as const fns. Validates overflowing patch/build bits via runtime
assert (const-friendly). 5 unit tests cover the AE 1.6.1170 constant,
Mora v0.1.0, max-width values, and both overflow cases."
```

---

### Task 5: Implement the ABI structs in `ffi.rs`

**Files:**
- Modify: `crates/skse-rs/src/ffi.rs`

- [ ] **Step 1: Write the struct definitions**

```bash
cat > crates/skse-rs/src/ffi.rs <<'EOF'
//! SKSE C-ABI types.
//!
//! These structs mirror the layouts in `CommonLibSSE-NG/include/SKSE/`
//! (see `docs/src/skse-rs-ffi-reference.md`). The types here are
//! unsafe to construct outside of their intended roles — they model
//! wire-format contracts, not safe Rust values.

use core::ffi::{c_char, c_void};

/// SKSE `kMessaging` interface ID — passed to `SKSEInterface::query_interface`.
pub const KMESSAGING: u32 = 5;

/// Interface version of `SKSEMessagingInterface` at AE 1.6.x.
pub const MESSAGING_INTERFACE_VERSION: u32 = 2;

/// Message type constants from `CommonLibSSE-NG` `MessagingInterface::kXxx`.
#[allow(non_camel_case_types, dead_code)]
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageType {
    PostLoad = 0,
    PostPostLoad = 1,
    PreLoadGame = 2,
    PostLoadGame = 3,
    SaveGame = 4,
    DeleteGame = 5,
    InputLoaded = 6,
    NewGame = 7,
    DataLoaded = 8,
}

/// `PluginVersionData` — the `SKSEPlugin_Version` data export.
///
/// Size: 0x350 bytes (verified by compile-time assert below).
#[repr(C)]
pub struct PluginVersionData {
    /// Must equal 1.
    pub data_version: u32, // 0x000
    /// Packed via `PluginVersion::pack`.
    pub plugin_version: u32, // 0x004
    /// NUL-terminated UTF-8 plugin identifier. Max 255 bytes + NUL.
    pub plugin_name: [u8; 256], // 0x008
    /// NUL-terminated UTF-8 author string.
    pub author: [u8; 256], // 0x108
    /// NUL-terminated UTF-8 support email.
    pub support_email: [u8; 252], // 0x208
    /// Flags group A — bit 0 is `no_struct_use`.
    pub flags_a: u32, // 0x304
    /// Flags group B — bit 0 = address_library, bit 1 = sig_scanning, bit 2 = structs_post629.
    pub flags_b: u32, // 0x308
    /// Zero-terminated list of packed runtime versions. `[0; 16]` signals version-agnostic.
    pub compatible_versions: [u32; 16], // 0x30C
    /// Minimum SKSE version. 0 = any.
    pub xse_minimum: u32, // 0x34C
}

const _: () = assert!(core::mem::size_of::<PluginVersionData>() == 0x350);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, plugin_name) == 0x008);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, author) == 0x108);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, support_email) == 0x208);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, flags_a) == 0x304);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, flags_b) == 0x308);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, compatible_versions) == 0x30C);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, xse_minimum) == 0x34C);

/// Bit masks for `PluginVersionData::flags_b`.
pub mod flags_b {
    pub const ADDRESS_LIBRARY: u32 = 1 << 0;
    pub const SIG_SCANNING: u32 = 1 << 1;
    pub const STRUCTS_POST629: u32 = 1 << 2;
}

/// `PluginInfo` — out-parameter of `SKSEPlugin_Query`.
///
/// Size: 0x10 bytes.
#[repr(C)]
pub struct PluginInfo {
    pub info_version: u32,     // 0x00
    _pad: u32,                 // 0x04 — implicit padding before 8-byte ptr
    pub name: *const c_char,   // 0x08 — pointer to static NUL-terminated string
    pub version: u32,          // 0x10? — wait, let's verify
    _pad2: u32,                // rounds to next alignment
}

// PluginInfo layout: 4 (info_version) + 4 (pad) + 8 (name*) + 4 (version) + 4 (pad) = 24 bytes
// The canonical SKSE layout is actually 0x18 = 24 bytes. Verify with the size_of assert.
const _: () = assert!(core::mem::size_of::<PluginInfo>() == 0x18);

impl Default for PluginInfo {
    fn default() -> Self {
        Self {
            info_version: 0,
            _pad: 0,
            name: core::ptr::null(),
            version: 0,
            _pad2: 0,
        }
    }
}

/// `SKSEInterface` — the first argument to `SKSEPlugin_Load`.
///
/// Size: 0x30 bytes.
#[repr(C)]
pub struct SKSEInterface {
    pub skse_version: u32,                                    // 0x00
    pub runtime_version: u32,                                 // 0x04
    pub editor_version: u32,                                  // 0x08
    pub is_editor: u32,                                       // 0x0C
    pub query_interface: unsafe extern "C" fn(u32) -> *mut c_void, // 0x10
    pub get_plugin_handle: unsafe extern "C" fn() -> u32,     // 0x18
    pub get_release_index: unsafe extern "C" fn() -> u32,     // 0x20
    pub get_plugin_info: unsafe extern "C" fn(*const c_char) -> *const c_void, // 0x28
}

const _: () = assert!(core::mem::size_of::<SKSEInterface>() == 0x30);

/// `SKSEMessagingInterface` — obtained by `query_interface(KMESSAGING)`.
///
/// Size: 0x20 bytes (4 + 4 padding + 8 + 8 + 8 = 32).
#[repr(C)]
pub struct SKSEMessagingInterface {
    pub interface_version: u32, // 0x00
    _pad: u32,                  // 0x04
    pub register_listener: unsafe extern "C" fn(
        handle: u32,
        sender: *const c_char,
        callback: *mut c_void,
    ) -> bool, // 0x08
    pub dispatch: unsafe extern "C" fn(
        handle: u32,
        msg_type: u32,
        data: *mut c_void,
        data_len: u32,
        receiver: *const c_char,
    ) -> bool, // 0x10
    pub get_event_dispatcher: unsafe extern "C" fn(u32) -> *mut c_void, // 0x18
}

const _: () = assert!(core::mem::size_of::<SKSEMessagingInterface>() == 0x20);

/// Signature of an SKSE messaging callback.
///
/// The callback receives a pointer to an `SKSEMessage` and must not retain
/// the pointer past the callback's return.
pub type MessagingCallback = unsafe extern "C" fn(msg: *mut SKSEMessage);

/// `SKSEMessage` — the struct passed to a messaging-interface callback.
#[repr(C)]
pub struct SKSEMessage {
    pub sender: *const c_char,
    pub msg_type: u32,
    _pad: u32,
    pub data_len: u32,
    _pad2: u32,
    pub data: *mut c_void,
}
EOF
```

**Important:** the exact size of `PluginInfo` depends on tail padding decisions. SKSE's documented canonical size is `0x18`. The struct `{ u32, u32-pad, *const u8, u32, u32-pad }` is:
- 4 (info_version)
- 4 (pad to 8)
- 8 (name ptr)
- 4 (version)
- 4 (tail pad to 8-byte alignment for array use)

That's 24 bytes = `0x18`. The `const _: () = assert!` catches any layout drift.

- [ ] **Step 2: Build it — size asserts are the test**

```bash
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Expected: both succeed. Each `const _: () = assert!(...)` runs at compile time; if any struct size differs from its documented value, the build fails with `evaluation of constant value failed`.

- [ ] **Step 3: Add runtime layout tests for belt-and-suspenders**

Compile-time asserts only catch size. We also want to verify that our `const` offsets match `offset_of!` at runtime (they should; compile-time asserts cover this too, but the tests are useful as documentation and run on both Linux and Windows-MSVC).

```bash
mkdir -p crates/skse-rs/tests
cat > crates/skse-rs/tests/layout.rs <<'EOF'
//! Integration tests for `skse-rs` ABI struct layouts.
//!
//! If any of these fail, the SKSE plugin DLL will fail to load
//! silently — SKSE rejects plugins with a `SKSEPlugin_Version`
//! whose layout doesn't match the expected size.

use skse_rs::ffi::*;

#[test]
fn plugin_version_data_is_0x350_bytes() {
    assert_eq!(std::mem::size_of::<PluginVersionData>(), 0x350);
}

#[test]
fn plugin_version_data_field_offsets_match_spec() {
    // Offsets from docs/src/skse-rs-ffi-reference.md
    assert_eq!(std::mem::offset_of!(PluginVersionData, data_version), 0x000);
    assert_eq!(std::mem::offset_of!(PluginVersionData, plugin_version), 0x004);
    assert_eq!(std::mem::offset_of!(PluginVersionData, plugin_name), 0x008);
    assert_eq!(std::mem::offset_of!(PluginVersionData, author), 0x108);
    assert_eq!(std::mem::offset_of!(PluginVersionData, support_email), 0x208);
    assert_eq!(std::mem::offset_of!(PluginVersionData, flags_a), 0x304);
    assert_eq!(std::mem::offset_of!(PluginVersionData, flags_b), 0x308);
    assert_eq!(std::mem::offset_of!(PluginVersionData, compatible_versions), 0x30C);
    assert_eq!(std::mem::offset_of!(PluginVersionData, xse_minimum), 0x34C);
}

#[test]
fn plugin_info_is_0x18_bytes() {
    assert_eq!(std::mem::size_of::<PluginInfo>(), 0x18);
}

#[test]
fn skse_interface_is_0x30_bytes() {
    assert_eq!(std::mem::size_of::<SKSEInterface>(), 0x30);
}

#[test]
fn skse_messaging_interface_is_0x20_bytes() {
    assert_eq!(std::mem::size_of::<SKSEMessagingInterface>(), 0x20);
}

#[test]
fn message_type_k_data_loaded_is_8() {
    assert_eq!(MessageType::DataLoaded as u32, 8);
}

#[test]
fn kmessaging_is_5() {
    assert_eq!(KMESSAGING, 5);
}
EOF
```

- [ ] **Step 4: Run the tests**

```bash
cargo test --package skse-rs --test layout
```

Expected: 7 tests pass.

- [ ] **Step 5: Commit**

```bash
git add crates/skse-rs/src/ffi.rs crates/skse-rs/tests/layout.rs
git commit -m "skse-rs: ABI type definitions for plugin interface

PluginVersionData (0x350), PluginInfo (0x18), SKSEInterface (0x30),
SKSEMessagingInterface (0x20), plus MessageType enum and KMESSAGING
constant. Sizes and field offsets validated at compile time via
const asserts + at runtime via integration tests in tests/layout.rs."
```

---

### Task 6: Verify end-to-end on both targets after Phase C

**Files:** none modified; verification.

- [ ] **Step 1: Full workspace check + test on Linux**

```bash
cargo check --workspace --all-targets
cargo test --workspace --all-targets
cargo clippy --workspace --all-targets -- -D warnings
```

Expected: all green.

- [ ] **Step 2: Full workspace Windows cross-compile**

```bash
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: clean. This is the one that matters — the ABI types must compile for the target we'll actually ship on.

- [ ] **Step 3: No commit**

Verification only. If anything failed, address it before Phase D.

---

## Phase D — Plugin Exports & `SksePlugin` Trait (Tasks 7-9)

### Task 7: Define the `SksePlugin` trait and load-error types in `plugin.rs`

**Files:**
- Modify: `crates/skse-rs/src/plugin.rs`

- [ ] **Step 1: Implement the trait + error types**

```bash
cat > crates/skse-rs/src/plugin.rs <<'EOF'
//! The idiomatic Rust entry point that plugin crates implement.
//!
//! Downstream crates implement [`SksePlugin`] and register via the
//! [`declare_plugin!`] macro, which generates the three required
//! C-ABI exports (`SKSEPlugin_Version`, `SKSEPlugin_Query`,
//! `SKSEPlugin_Load`) targeting an impl of this trait.

use crate::ffi::SKSEInterface;
use crate::version::PluginVersion;

/// Errors that can occur during plugin load. Returning `Err` from
/// [`SksePlugin::on_load`] causes the Rust-side macro to return
/// `false` from `SKSEPlugin_Load`, which SKSE interprets as a failed
/// plugin init (the DLL is unloaded).
#[derive(Debug, thiserror::Error)]
pub enum LoadError {
    #[error("log init failed: {0}")]
    LogInit(#[from] crate::log::LogInitError),
    #[error("SKSE messaging interface unavailable")]
    MessagingUnavailable,
    #[error("listener registration failed")]
    ListenerRegistrationFailed,
    #[error("plugin-specific: {0}")]
    Other(String),
}

/// Result alias used by [`SksePlugin::on_load`].
pub type LoadOutcome = Result<(), LoadError>;

/// Implemented by downstream SKSE plugins to expose their metadata and
/// init logic to the `skse-rs` ABI layer.
pub trait SksePlugin {
    /// Plugin identifier. Must be <= 255 bytes (NUL-terminated).
    const NAME: &'static str;
    /// Plugin author. <= 255 bytes (NUL-terminated).
    const AUTHOR: &'static str = "";
    /// Support email. <= 251 bytes (NUL-terminated).
    const SUPPORT_EMAIL: &'static str = "";
    /// Plugin version.
    const VERSION: PluginVersion;

    /// Called from `SKSEPlugin_Load`. The passed interface pointer is
    /// valid until the DLL unloads and is safe to stash in a static
    /// cell.
    ///
    /// # Safety
    /// `skse` must point to a valid `SKSEInterface` provided by SKSE.
    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome;
}
EOF
```

- [ ] **Step 2: Check + test**

```bash
cargo check --package skse-rs --all-targets
cargo test --package skse-rs --lib
```

Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/src/plugin.rs
git commit -m "skse-rs: SksePlugin trait + LoadError enum

Defines the idiomatic surface downstream plugin crates implement.
LoadError wraps log init failures, messaging unavailability,
listener registration errors, and a generic plugin-specific variant.
Trait captures name/author/email/version as associated constants plus
an on_load function that runs when SKSEPlugin_Load is called."
```

---

### Task 8: Implement the `declare_plugin!` macro and C-ABI exports

**Files:**
- Modify: `crates/skse-rs/src/plugin.rs`

This is the macro that generates the three required DLL exports from an `impl SksePlugin`. Downstream crates write `skse_rs::declare_plugin!(MyPlugin);` and get `SKSEPlugin_Version` / `SKSEPlugin_Query` / `SKSEPlugin_Load` for free.

- [ ] **Step 1: Append the macro to `plugin.rs`**

```bash
cat >> crates/skse-rs/src/plugin.rs <<'EOF'

/// Emit the three C-ABI exports (`SKSEPlugin_Version`,
/// `SKSEPlugin_Query`, `SKSEPlugin_Load`) for a type implementing
/// [`SksePlugin`].
///
/// ```ignore
/// use skse_rs::{declare_plugin, PluginVersion, SksePlugin, LoadOutcome};
/// use skse_rs::ffi::SKSEInterface;
///
/// struct MyPlugin;
/// impl SksePlugin for MyPlugin {
///     const NAME: &'static str = "MyPlugin";
///     const VERSION: PluginVersion = PluginVersion {
///         major: 0, minor: 1, patch: 0, build: 0,
///     };
///     unsafe fn on_load(_skse: &'static SKSEInterface) -> LoadOutcome {
///         Ok(())
///     }
/// }
///
/// declare_plugin!(MyPlugin);
/// ```
#[macro_export]
macro_rules! declare_plugin {
    ($plugin_ty:ty) => {
        /// Helper: copy `src` (a `&str`) into a zero-initialized `[u8; N]`
        /// byte buffer, NUL-terminated. Truncates with NUL fill if too long.
        const fn __skse_rs_copy_nul<const N: usize>(src: &str) -> [u8; N] {
            let mut out = [0u8; N];
            let bytes = src.as_bytes();
            let copy_len = if bytes.len() >= N { N - 1 } else { bytes.len() };
            let mut i = 0;
            while i < copy_len {
                out[i] = bytes[i];
                i += 1;
            }
            out
        }

        /// `SKSEPlugin_Version` — the plugin-info data export.
        #[allow(non_upper_case_globals)]
        #[no_mangle]
        pub static SKSEPlugin_Version: $crate::ffi::PluginVersionData =
            $crate::ffi::PluginVersionData {
                data_version: 1,
                plugin_version: <$plugin_ty as $crate::SksePlugin>::VERSION.pack(),
                plugin_name: __skse_rs_copy_nul::<256>(<$plugin_ty as $crate::SksePlugin>::NAME),
                author: __skse_rs_copy_nul::<256>(<$plugin_ty as $crate::SksePlugin>::AUTHOR),
                support_email: __skse_rs_copy_nul::<252>(
                    <$plugin_ty as $crate::SksePlugin>::SUPPORT_EMAIL,
                ),
                flags_a: 0,
                flags_b: $crate::ffi::flags_b::ADDRESS_LIBRARY,
                compatible_versions: [0; 16],
                xse_minimum: 0,
            };

        /// The canonical plugin name, as a static C string.
        #[allow(non_upper_case_globals)]
        static __SKSE_RS_PLUGIN_NAME_C: [u8; 256] =
            __skse_rs_copy_nul::<256>(<$plugin_ty as $crate::SksePlugin>::NAME);

        /// Legacy entry point — still required by SKSE.
        ///
        /// # Safety
        /// Called by SKSE's plugin loader with valid non-null pointers.
        #[allow(non_snake_case)]
        #[no_mangle]
        pub unsafe extern "C" fn SKSEPlugin_Query(
            _skse: *const $crate::ffi::SKSEInterface,
            info: *mut $crate::ffi::PluginInfo,
        ) -> bool {
            if info.is_null() {
                return false;
            }
            *info = $crate::ffi::PluginInfo {
                info_version: 1,
                _pad: 0,
                name: __SKSE_RS_PLUGIN_NAME_C.as_ptr() as *const core::ffi::c_char,
                version: <$plugin_ty as $crate::SksePlugin>::VERSION.pack(),
                _pad2: 0,
            };
            true
        }

        /// Real entry point — SKSE calls this with a valid interface.
        ///
        /// # Safety
        /// Called by SKSE's plugin loader; `skse` is valid for the lifetime
        /// of the DLL.
        #[allow(non_snake_case)]
        #[no_mangle]
        pub unsafe extern "C" fn SKSEPlugin_Load(
            skse: *const $crate::ffi::SKSEInterface,
        ) -> bool {
            if skse.is_null() {
                return false;
            }
            // SAFETY: SKSE guarantees this pointer is valid for the DLL lifetime.
            let skse_ref: &'static $crate::ffi::SKSEInterface =
                unsafe { &*skse };
            match <$plugin_ty as $crate::SksePlugin>::on_load(skse_ref) {
                Ok(()) => true,
                Err(_) => false,
            }
        }
    };
}
EOF
```

Rationale for design choices:
- `SKSEPlugin_Version` is a `static` (data export), not a `fn`. `plugin_version.pack()` needs to be callable in const context → `const fn`. Our `PluginVersion::pack` is `const fn`, good.
- The NUL-truncation helper `__skse_rs_copy_nul` is a `const fn` used inside the static initializer. Const while-loops are stable on 1.90.
- `SKSEPlugin_Query` memoizes the plugin name as a static array (can't return a `&str`'s raw pointer directly from a `const fn` without const-promotion shenanigans).
- Both function exports wrap `on_load` errors into `false` return codes — SKSE expects bool semantics and logs a failure if we return false.

- [ ] **Step 2: Verify it compiles**

```bash
cargo check --package skse-rs --all-targets
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Expected: clean. The macro itself doesn't expand until something uses it, so this just confirms the surrounding code compiles.

- [ ] **Step 3: Write a compile-only integration test that uses the macro**

This verifies the macro expands cleanly without linking it (we don't actually load the plugin in a unit test — that happens in Phase F's Skyrim smoke test).

```bash
cat > crates/skse-rs/tests/macro_expansion.rs <<'EOF'
//! Compile-only test for the `declare_plugin!` macro expansion.
//!
//! We can't usefully *run* the expanded plugin exports — they assume
//! a live SKSE host. But we can verify the macro expands without
//! compile errors, which proves the generated C-ABI items are
//! well-formed.
//!
//! This test defines a stub plugin in a `mod`-local scope so the
//! `#[no_mangle]` statics don't conflict with other tests.

// The stub plugin itself. No `on_load` body — we won't run it.
struct CompileOnlyPlugin;

impl skse_rs::SksePlugin for CompileOnlyPlugin {
    const NAME: &'static str = "CompileOnlyPlugin";
    const VERSION: skse_rs::PluginVersion = skse_rs::PluginVersion {
        major: 0,
        minor: 1,
        patch: 0,
        build: 0,
    };
    unsafe fn on_load(_skse: &'static skse_rs::ffi::SKSEInterface) -> skse_rs::LoadOutcome {
        Ok(())
    }
}

skse_rs::declare_plugin!(CompileOnlyPlugin);

#[test]
fn macro_expands_successfully() {
    // The generated SKSEPlugin_Version is a `pub static` at the test
    // file's module scope — accessible directly.
    let name_bytes = &SKSEPlugin_Version.plugin_name;
    // First 17 bytes should be "CompileOnlyPlugin"
    assert_eq!(&name_bytes[..17], b"CompileOnlyPlugin");
    // Byte 17 should be NUL
    assert_eq!(name_bytes[17], 0);
    // data_version should be 1
    assert_eq!(SKSEPlugin_Version.data_version, 1);
}
EOF
```

- [ ] **Step 4: Run the test**

```bash
cargo test --package skse-rs --test macro_expansion
```

Expected: passes. Note: this test links the `#[no_mangle]` exports into the test binary, which is why we can `extern "C"` reference them.

- [ ] **Step 5: Windows cross-compile**

```bash
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc --tests
```

Expected: clean. This confirms the macro generates valid Windows-ABI exports.

- [ ] **Step 6: Commit**

```bash
git add crates/skse-rs/src/plugin.rs crates/skse-rs/tests/macro_expansion.rs
git commit -m "skse-rs: declare_plugin! macro generating the three DLL exports

Macro expansion generates SKSEPlugin_Version (static data export),
SKSEPlugin_Query, and SKSEPlugin_Load from an impl SksePlugin.
Compile-only test asserts the expansion is well-formed and the
plugin_name byte array is filled correctly."
```

---

### Task 9: Refactor `lib.rs` re-exports

**Files:**
- Modify: `crates/skse-rs/src/lib.rs`

Replace the Phase B placeholder re-exports (`declare_plugin` was a stub macro) with the real ones. Plan 2's trait/macro are the real final thing.

- [ ] **Step 1: Clean up re-exports**

The current lib.rs already has:
```rust
pub use plugin::{declare_plugin, LoadError, LoadOutcome, SksePlugin};
```

`declare_plugin` is `#[macro_export]` and available at crate root via `skse_rs::declare_plugin!` — the `pub use` is redundant (macros are re-exported automatically from their `#[macro_export]` site). Remove it to avoid confusion.

```bash
cat > crates/skse-rs/src/lib.rs <<'EOF'
//! `skse-rs` — Rust-native SKSE plugin framework.
//!
//! Clean-room Rust port of the SKSE plugin infrastructure Mora needs.
//! Not a binding to the C++ CommonLibSSE-NG library; every ABI type
//! is defined directly in Rust with layout validated by compile-time
//! size asserts.
//!
//! See `docs/src/skse-rs-ffi-reference.md` for the source-of-truth
//! layout reference.
//!
//! # Using `skse-rs` in a plugin crate
//!
//! ```ignore
//! use skse_rs::{declare_plugin, PluginVersion, SksePlugin, LoadOutcome};
//! use skse_rs::ffi::SKSEInterface;
//!
//! struct MyPlugin;
//! impl SksePlugin for MyPlugin {
//!     const NAME: &'static str = "MyPlugin";
//!     const VERSION: PluginVersion = PluginVersion {
//!         major: 0, minor: 1, patch: 0, build: 0,
//!     };
//!     unsafe fn on_load(_: &'static SKSEInterface) -> LoadOutcome { Ok(()) }
//! }
//!
//! declare_plugin!(MyPlugin);
//! ```
//!
//! # M1 Foundation scope
//!
//! Plan 2 (this milestone's first half) implements:
//! - ABI types (`PluginVersionData`, `PluginInfo`, `SKSEInterface`,
//!   `SKSEMessagingInterface`, messaging IDs).
//! - The three plugin exports (via the [`declare_plugin!`] macro).
//! - File-based logger writing to the Skyrim log directory.
//! - `SksePlugin` trait + `declare_plugin!` macro for downstream
//!   crates to opt in cleanly.
//!
//! Plan 3 adds: Address Library parser, relocation layer, game type
//! layouts, `TESDataHandler` form lookup, `AddKeyword` re-implementation.

pub mod ffi;
pub mod log;
pub mod plugin;
pub mod version;

pub use plugin::{LoadError, LoadOutcome, SksePlugin};
pub use version::{PluginVersion, RuntimeVersion};

// `declare_plugin!` is re-exported from its `#[macro_export]` site
// and callable as `skse_rs::declare_plugin!(...)`.
EOF
```

- [ ] **Step 2: Verify**

```bash
cargo check --workspace --all-targets
cargo test --package skse-rs --all-targets
```

Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/src/lib.rs
git commit -m "skse-rs: tidy lib.rs re-exports

Macros export via #[macro_export] (pub use isn't needed). Updated
doc comment with working usage example referencing the real macro."
```

---

## Phase E — SKSE Log Writer (Tasks 10-11)

### Task 10: Implement the log-directory resolver

**Files:**
- Modify: `crates/skse-rs/src/log.rs`

SKSE plugins log to `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\<plugin_name>.log`. This task resolves that path on Windows (using `SHGetKnownFolderPath(FOLDERID_Documents)`) and falls back on non-Windows (for unit tests).

- [ ] **Step 1: Implement `log.rs`**

```bash
cat > crates/skse-rs/src/log.rs <<'EOF'
//! File-based logger writing to the SKSE plugin log directory.
//!
//! Path on Windows:
//! `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\<plugin>.log`
//!
//! Created on first write. Overwrites any previous log for the same
//! plugin. On non-Windows platforms (tests), falls back to
//! `$TMPDIR/skse-rs-<plugin>.log`.

use std::fs::{File, OpenOptions};
use std::io::{self, Write};
use std::path::PathBuf;
use std::sync::Mutex;

/// Initialization error for the logger.
#[derive(Debug, thiserror::Error)]
pub enum LogInitError {
    #[error("failed to resolve SKSE log directory: {0}")]
    PathResolution(String),
    #[error("failed to create log file: {0}")]
    FileOpen(#[from] io::Error),
}

/// An opened SKSE log file. Use [`Logger::write_line`] to append a
/// line; the logger flushes after each write (durable on crashes).
pub struct Logger {
    file: Mutex<File>,
    path: PathBuf,
}

impl Logger {
    /// Open (or create) the log for a plugin with the given name.
    pub fn open(plugin_name: &str) -> Result<Self, LogInitError> {
        let path = resolve_log_path(plugin_name)?;
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(LogInitError::FileOpen)?;
        }
        let file = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(&path)
            .map_err(LogInitError::FileOpen)?;
        Ok(Logger {
            file: Mutex::new(file),
            path,
        })
    }

    /// Write a single line (no trailing newline needed).
    pub fn write_line(&self, line: &str) -> io::Result<()> {
        let mut guard = self.file.lock().expect("log mutex poisoned");
        writeln!(guard, "{}", line)?;
        guard.flush()
    }

    /// Path the logger is writing to. Useful for diagnostics.
    pub fn path(&self) -> &std::path::Path {
        &self.path
    }
}

/// Resolve the log file path for the given plugin name.
fn resolve_log_path(plugin_name: &str) -> Result<PathBuf, LogInitError> {
    let dir = resolve_log_dir()?;
    Ok(dir.join(format!("{}.log", plugin_name)))
}

/// Resolve the SKSE log *directory*.
#[cfg(windows)]
fn resolve_log_dir() -> Result<PathBuf, LogInitError> {
    use widestring::U16CString;
    use windows_sys::Win32::{
        Foundation::S_OK,
        UI::Shell::{FOLDERID_Documents, SHGetKnownFolderPath},
    };

    unsafe {
        let mut path_ptr: *mut u16 = core::ptr::null_mut();
        let hr = SHGetKnownFolderPath(&FOLDERID_Documents, 0, std::ptr::null_mut(), &mut path_ptr);
        if hr != S_OK {
            return Err(LogInitError::PathResolution(format!(
                "SHGetKnownFolderPath(Documents) returned HRESULT 0x{:08X}",
                hr
            )));
        }
        let path_str = U16CString::from_ptr_str(path_ptr).to_string_lossy();
        // Caller owns path_ptr — must CoTaskMemFree.
        windows_sys::Win32::System::Com::CoTaskMemFree(path_ptr as *const _);
        let mut p = PathBuf::from(path_str);
        p.push("My Games");
        p.push("Skyrim Special Edition");
        p.push("SKSE");
        Ok(p)
    }
}

#[cfg(not(windows))]
fn resolve_log_dir() -> Result<PathBuf, LogInitError> {
    // Non-Windows: fall back to tmpdir for unit tests.
    Ok(std::env::temp_dir().join("skse-rs-logs"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn logger_writes_to_file() {
        let name = format!("test-logger-{}", std::process::id());
        let logger = Logger::open(&name).expect("open log");
        logger.write_line("hello").expect("write");
        logger.write_line("world").expect("write");
        let contents = std::fs::read_to_string(logger.path()).expect("read");
        assert_eq!(contents, "hello\nworld\n");
        std::fs::remove_file(logger.path()).ok();
    }

    #[test]
    fn logger_overwrites_existing_file() {
        let name = format!("test-logger-truncate-{}", std::process::id());
        // First session
        let logger1 = Logger::open(&name).unwrap();
        logger1.write_line("first-session").unwrap();
        drop(logger1);
        // Second session — should truncate
        let logger2 = Logger::open(&name).unwrap();
        logger2.write_line("second-session").unwrap();
        let contents = std::fs::read_to_string(logger2.path()).unwrap();
        assert_eq!(contents, "second-session\n");
        std::fs::remove_file(logger2.path()).ok();
    }
}
EOF
```

- [ ] **Step 2: Run the tests on Linux**

```bash
cargo test --package skse-rs --lib log::tests
```

Expected: 2 tests pass (on Linux they write to `/tmp/skse-rs-logs/`).

- [ ] **Step 3: Add required `windows-sys` feature for `CoTaskMemFree`**

`CoTaskMemFree` lives under `Win32_System_Com`. We need to add it to the features list.

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/Cargo.toml")
text = p.read_text()
marker = '    "Win32_UI_Shell_Common",\n'
new_line = '    "Win32_System_Com",\n'
if new_line not in text:
    text = text.replace(marker, marker + new_line, 1)
    p.write_text(text)
PY
grep -A1 'Win32_UI_Shell_Common' crates/skse-rs/Cargo.toml
```

Expected: confirms `Win32_System_Com` is now in the feature list.

- [ ] **Step 4: Windows cross-compile**

```bash
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add crates/skse-rs/src/log.rs crates/skse-rs/Cargo.toml
git commit -m "skse-rs: file-based Logger targeting the SKSE log directory

Resolves %USERPROFILE%\\Documents\\My Games\\Skyrim Special Edition\\SKSE\\
via SHGetKnownFolderPath(Documents). Opens plugin-name.log truncating
any previous contents; writes are flushed per line for crash durability.
Non-Windows fallback writes to the system temp dir for unit tests."
```

---

### Task 11: Expose `Logger` from the crate root for convenience

**Files:**
- Modify: `crates/skse-rs/src/lib.rs`

- [ ] **Step 1: Add re-export**

Append the `Logger` and `LogInitError` types to the crate-root re-exports (they're commonly used together with `SksePlugin`).

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/skse-rs/src/lib.rs")
text = p.read_text()
old = 'pub use plugin::{LoadError, LoadOutcome, SksePlugin};'
new = '''pub use log::{LogInitError, Logger};
pub use plugin::{LoadError, LoadOutcome, SksePlugin};'''
assert old in text, "expected re-export line not found"
text = text.replace(old, new)
p.write_text(text)
PY
grep -A3 '^pub use' crates/skse-rs/src/lib.rs
```

- [ ] **Step 2: Verify**

```bash
cargo check --workspace --all-targets
cargo test --package skse-rs
cargo xwin check --package skse-rs --target x86_64-pc-windows-msvc
```

Expected: all green.

- [ ] **Step 3: Commit**

```bash
git add crates/skse-rs/src/lib.rs
git commit -m "skse-rs: re-export Logger and LogInitError from crate root

Paired with SksePlugin in the common import block since plugins always
open a log in on_load."
```

---

## Phase F — The Smoke Plugin (Tasks 12-13)

### Task 12: Implement `skse-rs-smoke` as the end-to-end user

**Files:**
- Modify: `crates/skse-rs-smoke/src/lib.rs`

- [ ] **Step 1: Write the smoke plugin**

```bash
cat > crates/skse-rs-smoke/src/lib.rs <<'EOF'
//! `SkseRsSmoke` — smoke-test SKSE plugin exercising `skse-rs`.
//!
//! The smallest plugin that actually *does* something observable:
//! opens a log, writes "Hello from skse-rs" and a timestamp, exits.
//! Used to verify that a pure-Rust SKSE plugin loads, runs, and
//! reports successfully inside a live Skyrim via the SKSE loader.
//!
//! Log path:
//! `<Documents>\My Games\Skyrim Special Edition\SKSE\SkseRsSmoke.log`

// The cdylib output must be named SkseRsSmoke (SKSE plugin convention).
#![allow(non_snake_case)]

use skse_rs::ffi::SKSEInterface;
use skse_rs::{declare_plugin, LoadOutcome, Logger, PluginVersion, SksePlugin};

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
                "SKSE runtime version: 0x{:08x}",
                skse.runtime_version
            ))
            .ok();
        logger.write_line(&format!("plugin log: {}", logger.path().display())).ok();
        Ok(())
    }
}

declare_plugin!(SkseRsSmoke);
EOF
```

- [ ] **Step 2: Check it**

```bash
cargo check --package skse-rs-smoke
cargo xwin check --package skse-rs-smoke --target x86_64-pc-windows-msvc
```

Expected: both succeed.

- [ ] **Step 3: Cross-compile the actual DLL**

```bash
cargo xwin build --release --package skse-rs-smoke --target x86_64-pc-windows-msvc
ls -la target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll
```

Expected: a `SkseRsSmoke.dll` file exists. Size will be small (~50-150KB depending on Rust release stripping).

- [ ] **Step 4: Verify DLL exports are present**

Use `llvm-objdump` (usually available alongside the Rust toolchain) or `strings` to sanity-check the three exports.

```bash
llvm-objdump --private-headers target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll 2>/dev/null | grep -E "(SKSEPlugin_(Version|Query|Load))" | head
```

Expected: three matches — `SKSEPlugin_Version`, `SKSEPlugin_Query`, `SKSEPlugin_Load`. If `llvm-objdump` isn't available, fall back to `strings target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll | grep SKSEPlugin_` (less precise but confirms the names are embedded).

- [ ] **Step 5: Commit**

```bash
git add crates/skse-rs-smoke/src/lib.rs
git commit -m "skse-rs-smoke: implement smoke plugin using skse-rs

On load: opens SkseRsSmoke.log, writes a hello line + the SKSE runtime
version + its own log path. Exercises every part of skse-rs Plan 2
(ABI types, declare_plugin!, Logger) end-to-end."
```

---

### Task 13: Wire up an integration test case for the smoke plugin

**Files:**
- Create: `tests/integration/skse-rs-smoke/check.sh`
- Create: `tests/integration/skse-rs-smoke/README.md`

The integration test runs under the self-hosted Skyrim runner (per
`docs/src/integration-testing.md`). It stages the smoke DLL into
Skyrim's Data/SKSE/Plugins, launches Skyrim under Proton, waits for
the game to reach main menu, inspects the log, and exits.

Note: as of Plan 2, the `skyrim-integration` CI job is still `if: false`
and this case is therefore NOT part of PR CI yet. The case is committed
so the local `run-skyrim-test.sh` flow can exercise it, and so Plan 3 /
M5 can flip the CI gate and have something real to run.

- [ ] **Step 1: Write the check script**

```bash
mkdir -p tests/integration/skse-rs-smoke
cat > tests/integration/skse-rs-smoke/check.sh <<'EOF'
#!/usr/bin/env bash
# skse-rs-smoke: the pure-Rust plugin must load and write its log line.
#
# This case does not use the TCP harness — it validates only that the
# plugin was loaded and ran its on_load. A later case (added by Plan 3)
# will exercise real game state via the TCP harness.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/../_lib/check_common.sh"

trap stash_runtime_logs EXIT

# Wait for Skyrim to reach the main menu.
wait_for_main_menu || exit $?

# Locate the plugin's log. Same My Games path the plugin uses.
LOG="$SKYRIM_PROFILE_DIR/SKSE/SkseRsSmoke.log"

if [[ ! -s "$LOG" ]]; then
    _err "skse-rs-smoke: SkseRsSmoke.log missing or empty at $LOG"
    exit 1
fi

if ! grep -q "^Hello from skse-rs$" "$LOG"; then
    _err "skse-rs-smoke: log does not contain expected greeting line"
    _err "log contents:"
    sed 's/^/  /' "$LOG" >&2
    exit 1
fi

echo "[check] skse-rs-smoke: PASS"
EOF
chmod +x tests/integration/skse-rs-smoke/check.sh
```

Rationale:
- `wait_for_main_menu` is a helper in `_lib/check_common.sh` that polls
  Skyrim's process or an indicator file until the game has fully booted.
  If it's not there today, Plan 3 / M5 adds it alongside the TCP harness
  port.
- Plan 2 deliberately avoids the TCP harness — the smoke plugin doesn't
  open a TCP server. Log inspection is sufficient for this phase.

- [ ] **Step 2: Write the case README**

```bash
cat > tests/integration/skse-rs-smoke/README.md <<'EOF'
# skse-rs-smoke

Pure-Rust SKSE plugin (built via `skse-rs`) must load successfully
into Skyrim SE and write "Hello from skse-rs" to its log file at
`<Documents>\My Games\Skyrim Special Edition\SKSE\SkseRsSmoke.log`.

No game state is read or modified — this validates only that a Rust
cdylib produced by `cargo xwin build -p skse-rs-smoke --target
x86_64-pc-windows-msvc` is a valid SKSE plugin and that all of Plan 2's
ABI work (exports, log directory resolution, plugin version data) is
wire-compatible with SKSE's loader.

**CI gate:** this case is not yet wired into the self-hosted
`skyrim-integration` job — Plan 2 leaves that job at `if: false`.
Run manually via `run-skyrim-test.sh` on a dev box, or enable it in
Plan 3 / M5 when adding the TCP-harness-driven cases.
EOF
```

- [ ] **Step 3: Verify the files are committed in the right place**

```bash
ls -la tests/integration/skse-rs-smoke/
```

Expected: `check.sh` (executable) + `README.md`.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/skse-rs-smoke/
git commit -m "tests/integration: skse-rs-smoke case

Bash-based integration case that stages SkseRsSmoke.dll into a
Skyrim install, launches under Proton, and verifies the plugin's
log file contains the expected greeting. Not yet wired into CI
(skyrim-integration job stays if:false until Plan 3 / M5)."
```

---

## Phase G — Final Verification & Handoff (Task 14)

### Task 14: Full verification + push + open PR

**Files:** none modified.

- [ ] **Step 1: Clean build + full verification**

```bash
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
cargo xwin build --release --target x86_64-pc-windows-msvc -p skse-rs-smoke
```

Expected: all seven commands succeed. The final `cargo xwin build` produces `target/x86_64-pc-windows-msvc/release/SkseRsSmoke.dll`.

- [ ] **Step 2: Confirm expected test counts**

```bash
cargo test --package skse-rs 2>&1 | tail -15
```

Expected:
- `version::tests`: 5 passed
- `log::tests`: 2 passed
- `tests/layout.rs`: 7 passed
- `tests/macro_expansion.rs`: 1 passed

Total 15 tests, 0 failed.

- [ ] **Step 3: Push the branch**

```bash
git push -u origin m1-skse-rs-foundation
```

Expected: push succeeds. CI triggers.

- [ ] **Step 4: Watch CI**

```bash
gh run watch --exit-status 2>&1 | tail -10
```

Expected: four GitHub-hosted jobs (fmt/clippy/test/windows-cross) pass. `skyrim-integration` stays skipped (`if: false`).

- [ ] **Step 5: Open the PR**

```bash
gh pr create --base master --head m1-skse-rs-foundation --title "Rust + KID pivot — M1 Part 1: skse-rs foundation" --body "$(cat <<'PRBODY'
## Summary

Implements the non-game portion of spec milestone M1 per
`docs/superpowers/plans/2026-04-21-rust-kid-pivot-plan-2-skse-rs-foundation.md`:

- `skse-rs` ABI types (`PluginVersionData`, `PluginInfo`, `SKSEInterface`,
  `SKSEMessagingInterface`) with layout validated by compile-time asserts
  and runtime integration tests.
- `PluginVersion` / `RuntimeVersion` packing matching `REL::Version`.
- `SksePlugin` trait + `declare_plugin!` macro generating the three
  required DLL exports (`SKSEPlugin_Version`, `SKSEPlugin_Query`,
  `SKSEPlugin_Load`).
- File-based `Logger` resolving the Skyrim log directory via
  `SHGetKnownFolderPath(FOLDERID_Documents)`.
- `skse-rs-smoke` companion cdylib producing `SkseRsSmoke.dll`.
- `tests/integration/skse-rs-smoke/` case (not yet CI-gated).
- `docs/src/skse-rs-ffi-reference.md` — layout source-of-truth.

No game-memory interaction, no Address Library. Plan 3 (next PR) adds
those and completes M1.

## Test plan

- [x] `cargo test --workspace` — 15 tests, 0 failures.
- [x] `cargo clippy --all-targets -- -D warnings` clean.
- [x] `cargo fmt --check` clean.
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean.
- [x] `cargo xwin build -p skse-rs-smoke --release` produces `SkseRsSmoke.dll`.
- [ ] Manual smoke: load `SkseRsSmoke.dll` in Skyrim SE, confirm
  `SkseRsSmoke.log` contains `Hello from skse-rs`. Reviewer should
  run this on their dev box per `docs/src/integration-testing.md`.

## Next up

Plan 3 — `skse-rs` game interop (Address Library parser, REL layer,
TESDataHandler singleton + LookupForm, BGSKeywordForm::AddKeyword
re-implementation).
PRBODY
)"
```

Expected: PR URL printed.

- [ ] **Step 6: No commit — handoff to human merge**

Once CI is green and someone has sanity-tested the DLL in Skyrim (or you
decide to trust the unit tests), merge and move on to Plan 3.

---

## Completion criteria

- [ ] `cargo test --workspace` passes all 15 new tests.
- [ ] `cargo xwin build -p skse-rs-smoke --release` produces a well-formed `SkseRsSmoke.dll` exporting `SKSEPlugin_Version`, `SKSEPlugin_Query`, `SKSEPlugin_Load`.
- [ ] All compile-time size asserts in `ffi.rs` pass on both `x86_64-unknown-linux-gnu` and `x86_64-pc-windows-msvc`.
- [ ] PR merged to `master`.
- [ ] (Optional but recommended) `SkseRsSmoke.dll` tested manually in Skyrim SE; `SkseRsSmoke.log` contains the expected greeting.

## Next plan

Once this plan is merged, write **Plan 3 — `skse-rs` Game Interop (M1
Part 2 of 2)**. That plan will cover:

1. Address Library v2 `.bin` format parser (`REL::ID` equivalent).
2. Relocation layer — resolve an Address Library ID to a function pointer using `GetModuleHandleW(null)` as image base.
3. Game type layouts (`TESForm`, `BGSKeywordForm`, `BGSKeyword`, `TESDataHandler`) as `#[repr(C)]` with `size_of` asserts citing `docs/src/skse-rs-ffi-reference.md` (which Plan 3 expands with the new layouts).
4. `TESDataHandler::LookupForm(localFormID, modName)` binding (helper that the CommonLibSSE-NG public API wraps over `LookupByID`).
5. `BGSKeywordForm::AddKeyword` — re-implementation that grows the `keywords` array via Skyrim's allocator (resolved via Address Library).
6. `skse-rs-smoke` upgraded to: look up "Iron Sword" (FormID `0x00012EB7` in `Skyrim.esm`), add `WeapMaterialIron` keyword, confirm via a second lookup.
7. Flip the CI `skyrim-integration` gate from `if: false` to a real condition; wire the upgraded smoke case into PR CI.

Plan 3 is the point where `skse-rs` becomes genuinely useful for Mora's
downstream work (M2: `mora-core`, `mora-esp`; M3: `mora-kid`; M5:
`mora-runtime`).
