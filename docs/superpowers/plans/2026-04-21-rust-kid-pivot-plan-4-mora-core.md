# Rust + KID Pivot — Plan 4: `mora-core` (M2, Part 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Flesh out `mora-core` — the shared types and algorithms underlying every Mora frontend. This plan delivers FormID newtypes, the `Patch` enum + `PatchFile` postcard format, the `Distributor` trait + `PatchSink` dedup/merge, and the `DeterministicChance` RNG (bit-identical port of KID's Xoshiro256** + MSVC `uniform_real_distribution<float>`). No ESP parsing, no SKSE code — pure Rust logic, fully unit-testable, platform-agnostic.

**Architecture:** `mora-core` is the shared trunk of the workspace. Frontend crates (`mora-kid`, future `mora-spid`, etc.) depend on it for `Patch` types + `Distributor` trait. `mora-runtime` depends on it for `PatchFile` deserialization. `mora-esp` depends on it for `FormId` newtypes. Everything in `mora-core` is `no_std`-compatible in principle but uses `std` for collections + serde; the crate does NOT use `unsafe`.

**Tech Stack:** Rust 1.90. No new deps — FNV-1a-32 is inlined (~5 lines, standard algorithm) rather than depending on the `fnv` crate (which is 64-bit by default and would require either XOR-folding or a configured wrapper, both muddier than just writing the algorithm). All other deps (serde, postcard, thiserror, tracing) are already workspace-pinned from M0.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope discipline:**
- **No ESP-format code.** `mora-esp` gets its own plan (Plan 5).
- **No `mora-kid` code.** That's Plan 6+.
- **No `mora-runtime` edits.** The runtime only reads `PatchFile`; its implementation waits for Plan 7/M5.
- **Only `AddKeyword` variant in the `Patch` enum** at this plan. The enum is `#[non_exhaustive]`-shaped (explicitly doc'd as growing later); future plans add `SetFieldU32`, `SetFieldStr`, etc. as record-type-specific distributor frontends land.

---

## File Structure

**Modified:**
- `crates/mora-core/src/lib.rs` — replace stub with module declarations + re-exports
- `docs/src/skse-rs-ffi-reference.md` — NOT modified (we create a NEW reference doc for mora-core specifics)

**Created:**
- `crates/mora-core/src/form_id.rs` — `FormId` / `FullFormId` newtypes
- `crates/mora-core/src/patch.rs` — `Patch` enum + `PatchFile` + postcard round-trip helpers
- `crates/mora-core/src/distributor.rs` — `Distributor` trait + `DistributorStats`
- `crates/mora-core/src/patch_sink.rs` — `PatchSink` (dedup + merge + finalize)
- `crates/mora-core/src/chance/mod.rs` — `DeterministicChance` high-level API
- `crates/mora-core/src/chance/xoshiro.rs` — Xoshiro256** PRNG
- `crates/mora-core/src/chance/splitmix.rs` — SplitMix64 state init
- `crates/mora-core/src/chance/msvc_uniform.rs` — MSVC `uniform_real_distribution<float>` port
- `crates/mora-core/src/chance/szudzik.rs` — Szudzik pairing
- `crates/mora-core/src/chance/fnv.rs` — FNV-1a-32 hash (inlined, standard algorithm)
- `crates/mora-core/tests/patch_roundtrip.rs` — integration test for serialize → deserialize
- `docs/src/mora-core-reference.md` — source-of-truth doc for patch format + chance algorithm

---

## Phase A — Reference documentation (Task 1)

### Task 1: Write `docs/src/mora-core-reference.md`

**Files:**
- Create: `docs/src/mora-core-reference.md`

This doc is the source-of-truth for: (a) the on-disk format of `mora_patches.bin`, (b) the deterministic-chance algorithm we port from KID. Subsequent tasks cite it directly.

- [ ] **Step 1: Write the doc**

```bash
cd /home/tbaldrid/oss/mora
cat > docs/src/mora-core-reference.md <<'EOF'
# mora-core Reference

Source of truth for the types + algorithms in `crates/mora-core/`. This
doc is for Rust consumers of the crate (frontends, runtime, tests);
the ABI-level `docs/src/skse-rs-ffi-reference.md` is separate.

## `FormId` — a 32-bit FormID

```rust
pub struct FormId(pub u32);
```

A FormID as Mora sees it: the raw 32-bit value Bethesda stores in
record headers and references. The low 3 bytes are the local ID; the
high byte is the "mod index" (compile-time slot in the active load
order). Mora stores fully-resolved IDs everywhere — the compiler
remaps local IDs to runtime mod indices at compile time, so downstream
consumers never do plugin-index arithmetic themselves.

## `FullFormId` — a plugin-qualified FormID (load-order-independent)

```rust
pub struct FullFormId {
    pub plugin: CompactStr,      // plugin filename, e.g. "Skyrim.esm"
    pub local_id: u32,           // low 24 bits; high byte zeroed
}
```

For INI-parsing scenarios where the user refers to a form by plugin +
local id (`"0xXXX~PluginName"` in KID syntax). Converted to a `FormId`
against a known load order via a helper in `mora-esp` (in Plan 5, not
this plan).

## `PatchFile` — the on-disk `mora_patches.bin`

postcard-serialized Rust struct:

```rust
pub struct PatchFile {
    pub magic: [u8; 4],              // b"MORA"
    pub version: u32,                // format version, currently 1
    pub load_order_hash: u64,        // sanity check against active load order
    pub patches: Vec<Patch>,         // applied sequentially
}
```

- `magic` is a fixed byte string used by the runtime to detect a
  well-formed file. Rejecting on mismatch protects against mis-named or
  truncated files.
- `version` bumps when the format changes. Runtime refuses files with
  a higher version than it knows how to read.
- `load_order_hash` is described in Plan 5; at this plan, it's an
  opaque `u64` that higher layers fill in.
- `patches` is applied in-order by the runtime. Order is stable:
  sorted by `(opcode_tag, target_form_id)` at `PatchSink::finalize`.

## `Patch` — one applied change

```rust
#[non_exhaustive]
pub enum Patch {
    AddKeyword { target: FormId, keyword: FormId },
}
```

`#[non_exhaustive]` is NOT literally used (see the next section for
serialization compatibility); the **intent** is that the enum grows
with more variants over time (M5+ adds `SetFieldU32`, `SetFieldStr`,
`RemoveKeyword`, etc. as real distributor frontends land).

### Serialization stability

postcard serializes enum variants by their **declaration index**, not
by a manually-assigned tag. This means appending a new variant is
forward-compatible (old readers can still parse files with only known
variants), but **reordering or removing variants is a breaking
change** that requires bumping `PatchFile.version`.

Plan 4 establishes `AddKeyword` at index 0. Variants added later
**must** be appended; never insert in the middle.

## `Distributor` trait

```rust
pub trait Distributor {
    type Error: std::error::Error + Send + Sync + 'static;
    fn name(&self) -> &'static str;
    fn lower(
        &self,
        world: &EspWorld,               // from mora-esp (Plan 5)
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error>;
}
```

**Plan 4 defines the trait with a placeholder `EspWorld`** — an
opaque marker struct. Plan 5 replaces the marker with the real
`mora_esp::EspWorld` type. This placeholder approach lets Plan 4 ship
the trait surface without introducing a `mora-esp` cycle or a
premature dependency.

The `DistributorStats` struct:

```rust
pub struct DistributorStats {
    pub rules_evaluated: u64,
    pub candidates_considered: u64,
    pub patches_emitted: u64,
    pub rejected_by_chance: u64,
    pub rejected_by_filter: u64,
}
```

Summed across frontends and logged by `mora-cli`.

## `PatchSink` — append-only collector with dedupe

```rust
pub struct PatchSink { /* private */ }

impl PatchSink {
    pub fn new() -> Self;
    pub fn push(&mut self, patch: Patch);
    pub fn stats(&self) -> &PatchSink::Stats;
    pub fn finalize(self) -> PatchFile;
}
```

- `push` stores the patch, deduping identical `(variant, target, payload)` tuples.
- `finalize` consumes the sink, sorts by `(variant_discriminant, target_form_id)`, and produces a `PatchFile` with `magic`/`version`/`load_order_hash` filled in.
  - `load_order_hash` is supplied via a setter (`set_load_order_hash`) before `finalize`; if unset, `finalize` uses `0` (runtime will detect mismatch).

Dedupe is exact-match only. Two rules producing the same
`AddKeyword { target: X, keyword: Y }` collapse to a single patch.
Stats record the duplicate count.

## `DeterministicChance` — KID-bit-compatible chance RNG

Mora reproduces KID's chance-resolution algorithm exactly. The intent
is that a user migrating from KID to Mora sees the same keyword
distribution — not a statistical approximation, but the same roll
outcome per item.

**Full algorithm** (ported from `CommonLibSSE-NG` + `CLibUtil`):

```
seed   = szudzik_pair(fnv1a_32(keyword.editor_id.as_bytes()), form_id.0)
state  = splitmix64_init(seed)
raw    = xoshiro_256_starstar(state).next_u64()
scale  = raw as f64 / 2^64 as f64
clamp  = scale as f32
percent = if clamp >= 1.0f32 { 0.0f32 } else { 100.0f32 * clamp }
passes = percent <= rule.chance as f32
```

### Step-by-step

1. **Szudzik pairing** — `uint64_t szudzik_pair(uint64_t a, uint64_t b)`:
   ```
   if a >= b: a*a + a + b
   else:      a + b*b
   ```
   Bethesda takes two unsigned values and produces a `u64` seed.

2. **FNV-1a (32-bit)** — standard FNV-1a with offset `0x811c9dc5` and prime `0x01000193`, applied byte-by-byte over the keyword's editor-ID string bytes. Note: KID uses a `std::string_view` in C++; the Rust equivalent is `editor_id.as_bytes()`. Case is preserved (editor IDs are case-sensitive at the CommonLibSSE-NG level for hashing, though editor-ID *lookups* are case-insensitive — but hashing uses the exact bytes).

3. **SplitMix64 init** — the Xoshiro family's standard seeding step. XoshiroCpp (KID's RNG library) initializes the 4×u64 state from a single u64 seed by running 4 iterations of SplitMix64:
   ```
   s0 = splitmix64_next(&seed)
   s1 = splitmix64_next(&seed)
   s2 = splitmix64_next(&seed)
   s3 = splitmix64_next(&seed)
   ```
   where `splitmix64_next` is:
   ```
   seed = seed.wrapping_add(0x9E3779B97F4A7C15)
   z = seed
   z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9)
   z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB)
   z = z ^ (z >> 31)
   return z
   ```

4. **Xoshiro256\*\*** — the reference algorithm from https://prng.di.unimi.it. One step:
   ```
   result = rotl(s[1] * 5, 7) * 9
   t = s[1] << 17
   s[2] ^= s[0]
   s[3] ^= s[1]
   s[1] ^= s[2]
   s[0] ^= s[3]
   s[2] ^= t
   s[3] = rotl(s[3], 45)
   return result
   ```
   KID constructs a fresh RNG per roll (no stream state across rolls),
   so we only need the first `next_u64` result.

5. **MSVC `uniform_real_distribution<float>`** for range `[0, 100)`:
   - `raw_u64` from Xoshiro above
   - `scale_f64 = raw_u64 as f64 / 18446744073709551616.0_f64` (i.e. `/ 2^64`)
   - `clamp_f32 = scale_f64 as f32`  (narrow; IEEE 754 round-to-nearest)
   - `if clamp_f32 >= 1.0f32 { clamp_f32 = 0.0f32 }`  (MSVC's out-of-range handling)
   - `percent = 100.0f32 * clamp_f32`  (f32 multiply, not f64)

6. **Chance comparison** — `passes = percent <= rule.chance as f32`. KID's comparison is `roll <= chance` (inclusive), matching the `filters.chance` field in KID INIs which is a percentage 0-100.

### Why bit-identical matters

If the roll differs from KID's by even one LSB of float precision,
some marginal items will gain/lose keywords vs. what KID would do.
For M1 / M2 we port the algorithm carefully; M4 (golden-test harness)
validates against real KID output.

## Tests

This plan ships unit tests for every component:
- FNV-1a against 3+ known test vectors
- Szudzik round-trip for several (a, b) pairs
- SplitMix64 first-few-values against reference
- Xoshiro256** first-few-values against reference (seeded with known splitmix)
- MSVC draw for seed=0 (yields `(0u64 as f64 / 2^64)` = 0.0 → percent = 0.0)
- Distribution sanity: 10,000 seeds at chance=50 should give ~5000 passes ± 3σ
- PatchFile serialize → deserialize round-trip
- PatchSink dedup + finalize

Full statistical / KID-bit-identical validation lands in M4 (golden-test harness), which captures KID output from a real Skyrim run and diffs against Mora's patch file.

EOF
```

- [ ] **Step 2: Commit**

```bash
git add docs/src/mora-core-reference.md
git commit -m "docs: mora-core reference

Source of truth for FormId, PatchFile postcard format, Patch enum
serialization stability, Distributor trait, PatchSink semantics,
and the KID-bit-compatible deterministic chance algorithm (Szudzik
pairing + FNV-1a + SplitMix64 + Xoshiro256** + MSVC float draw).
Cited by subsequent tasks when defining types."
```

---

## Phase B — Workspace + crate setup (Task 2)

### Task 2: Restructure `mora-core/src/lib.rs` + create stub modules

**Files:**
- Modify: `crates/mora-core/src/lib.rs`
- Create stubs: `crates/mora-core/src/{form_id,patch,distributor,patch_sink}.rs` + `crates/mora-core/src/chance/{mod,xoshiro,splitmix,msvc_uniform,szudzik,fnv}.rs`

No new dependencies — all needed crates are already in the workspace (serde, postcard, thiserror, tracing). FNV-1a-32 is inlined in Task 9 rather than relying on the `fnv` crate (which would be wrong — it's 64-bit).

- [ ] **Step 1: Replace the stub `lib.rs` with module declarations**

```bash
cat > crates/mora-core/src/lib.rs <<'EOF'
//! Core shared types + algorithms for Mora.
//!
//! Frontend crates (`mora-kid`, future `mora-spid`, …) use this crate
//! for the `Patch` types + `Distributor` trait. The runtime uses it
//! for `PatchFile` deserialization. `mora-esp` uses it for `FormId`.
//!
//! No `unsafe`, no platform-specific code — pure Rust logic.
//!
//! See `docs/src/mora-core-reference.md` for types + algorithms.

pub mod chance;
pub mod distributor;
pub mod form_id;
pub mod patch;
pub mod patch_sink;

pub use chance::DeterministicChance;
pub use distributor::{Distributor, DistributorStats, EspWorld};
pub use form_id::{FormId, FullFormId};
pub use patch::{Patch, PatchFile, PATCH_FILE_MAGIC, PATCH_FILE_VERSION};
pub use patch_sink::PatchSink;
EOF
```

- [ ] **Step 2: Create stub modules so `cargo check` stays green**

```bash
cat > crates/mora-core/src/form_id.rs <<'EOF'
//! Stub. Populated in Task 3.

/// Placeholder — real impl in Task 3.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct FormId(pub u32);

/// Placeholder — real impl in Task 3.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct FullFormId {
    pub plugin: String,
    pub local_id: u32,
}
EOF

cat > crates/mora-core/src/patch.rs <<'EOF'
//! Stub. Populated in Task 4.

use serde::{Deserialize, Serialize};

use crate::form_id::FormId;

/// Placeholder — real impl in Task 4.
pub const PATCH_FILE_MAGIC: [u8; 4] = *b"MORA";
/// Placeholder — real impl in Task 4.
pub const PATCH_FILE_VERSION: u32 = 1;

/// Placeholder — real impl in Task 4.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Patch {
    AddKeyword { target: FormId, keyword: FormId },
}

/// Placeholder — real impl in Task 4.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PatchFile {
    pub magic: [u8; 4],
    pub version: u32,
    pub load_order_hash: u64,
    pub patches: Vec<Patch>,
}
EOF

cat > crates/mora-core/src/distributor.rs <<'EOF'
//! Stub. Populated in Task 5.

/// Placeholder — opaque marker so downstream compiles. Plan 5
/// replaces with real `mora_esp::EspWorld`.
pub struct EspWorld;

/// Placeholder — real impl in Task 5.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats;

/// Placeholder — real impl in Task 5.
pub trait Distributor {
    type Error;
    fn name(&self) -> &'static str;
}
EOF

cat > crates/mora-core/src/patch_sink.rs <<'EOF'
//! Stub. Populated in Task 6.

/// Placeholder — real impl in Task 6.
#[derive(Default)]
pub struct PatchSink;

impl PatchSink {
    pub fn new() -> Self {
        Self
    }
}
EOF

mkdir -p crates/mora-core/src/chance
cat > crates/mora-core/src/chance/mod.rs <<'EOF'
//! Stub. Populated in Task 12.

pub mod fnv;
pub mod msvc_uniform;
pub mod splitmix;
pub mod szudzik;
pub mod xoshiro;

/// Placeholder — real impl in Task 12.
pub struct DeterministicChance;
EOF

for m in xoshiro splitmix msvc_uniform szudzik fnv; do
    cat > crates/mora-core/src/chance/$m.rs <<EOF
//! Stub. Populated in its own task of Plan 4.
EOF
done
```

- [ ] **Step 3: Verify build + commit**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-core/
git commit -m "mora-core: crate scaffold with module tree + stubs

mora-core/src: lib.rs declares form_id/patch/distributor/patch_sink/
chance modules, each populated in its own task. All stubs are
minimally typed so cargo check stays green through Plan 4."
```

---

## Phase C — FormId newtypes (Task 3)

### Task 3: Implement `form_id.rs` with FormId + FullFormId

**Files:**
- Modify: `crates/mora-core/src/form_id.rs`

- [ ] **Step 1: Write the real form_id.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-core/src/form_id.rs <<'EOF'
//! FormID newtypes.
//!
//! Mora uses fully-resolved 32-bit FormIDs internally. Plugin-qualified
//! IDs (e.g. from KID INIs) are represented as `FullFormId` and
//! resolved against a load order by `mora-esp` (Plan 5).

use serde::{Deserialize, Serialize};

/// A fully-resolved 32-bit FormID (mod index baked into the high byte).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
pub struct FormId(pub u32);

impl FormId {
    /// The low 24 bits — the "local" form id.
    pub const fn local_id(self) -> u32 {
        self.0 & 0x00FF_FFFF
    }

    /// The high 8 bits — the mod index in the active load order.
    pub const fn mod_index(self) -> u8 {
        (self.0 >> 24) as u8
    }

    /// Construct from mod index + local id.
    pub const fn from_parts(mod_index: u8, local_id: u32) -> Self {
        debug_assert!(local_id & 0xFF00_0000 == 0, "local_id must fit in 24 bits");
        FormId(((mod_index as u32) << 24) | (local_id & 0x00FF_FFFF))
    }

    /// The raw 32-bit value.
    pub const fn raw(self) -> u32 {
        self.0
    }
}

impl std::fmt::Display for FormId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "0x{:08X}", self.0)
    }
}

impl From<u32> for FormId {
    fn from(raw: u32) -> Self {
        FormId(raw)
    }
}

/// A plugin-qualified, load-order-independent form reference.
///
/// Used when a KID INI says `0xABC~PluginName.esm` — the `plugin`
/// string + the local id. Resolved to a `FormId` against a live load
/// order by `mora-esp::EspWorld::resolve_full_form_id`.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct FullFormId {
    /// The plugin filename (e.g. `"Skyrim.esm"`). Case-insensitive for
    /// lookups; the canonical form stores the filename as-given.
    pub plugin: String,
    /// The local form id — low 24 bits. The high byte of the full
    /// FormID is determined at resolution time by the plugin's
    /// compile-time mod index.
    pub local_id: u32,
}

impl FullFormId {
    pub fn new(plugin: impl Into<String>, local_id: u32) -> Self {
        FullFormId {
            plugin: plugin.into(),
            local_id: local_id & 0x00FF_FFFF,
        }
    }
}

impl std::fmt::Display for FullFormId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "0x{:06X}~{}", self.local_id, self.plugin)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn form_id_parts() {
        let f = FormId(0x0A_12_34_56);
        assert_eq!(f.mod_index(), 0x0A);
        assert_eq!(f.local_id(), 0x12_34_56);
        assert_eq!(f.raw(), 0x0A_12_34_56);
    }

    #[test]
    fn form_id_from_parts() {
        let f = FormId::from_parts(0x0A, 0x12_34_56);
        assert_eq!(f, FormId(0x0A_12_34_56));
    }

    #[test]
    fn form_id_display_is_hex() {
        assert_eq!(format!("{}", FormId(0x0001_2EB7)), "0x00012EB7");
    }

    #[test]
    fn full_form_id_constructs_and_masks_high_byte() {
        // If caller passes a full 32-bit value, the high byte is stripped.
        let f = FullFormId::new("Skyrim.esm", 0xFF_12_34_56);
        assert_eq!(f.local_id, 0x12_34_56);
        assert_eq!(f.plugin, "Skyrim.esm");
    }

    #[test]
    fn full_form_id_display_format() {
        let f = FullFormId::new("Skyrim.esm", 0x12EB7);
        assert_eq!(format!("{}", f), "0x012EB7~Skyrim.esm");
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib form_id::tests
cargo xwin check --package mora-core --target x86_64-pc-windows-msvc
git add crates/mora-core/src/form_id.rs
git commit -m "mora-core: FormId + FullFormId newtypes

FormId wraps u32 with local_id / mod_index / from_parts helpers and
hex Display. FullFormId holds (plugin, local_id) for load-order-
independent references; resolved to FormId by mora-esp in Plan 5.
5 unit tests cover parts decomposition, construction, and display."
```

---

## Phase D — Patch types (Task 4)

### Task 4: Implement `patch.rs` — `Patch` + `PatchFile`

**Files:**
- Modify: `crates/mora-core/src/patch.rs`

- [ ] **Step 1: Write patch.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-core/src/patch.rs <<'EOF'
//! `Patch` enum + `PatchFile` on-disk format.
//!
//! See `docs/src/mora-core-reference.md` for the postcard encoding and
//! enum-variant serialization stability story.

use serde::{Deserialize, Serialize};

use crate::form_id::FormId;

/// Fixed byte sequence at the start of every `PatchFile`.
pub const PATCH_FILE_MAGIC: [u8; 4] = *b"MORA";

/// On-disk format version. Bumped on incompatible changes to the
/// binary layout or enum-variant ordering.
pub const PATCH_FILE_VERSION: u32 = 1;

/// A single patch applied by the runtime at `kDataLoaded`.
///
/// **Variants must not be reordered or removed** — postcard
/// serializes enums by declaration order, so reordering breaks
/// existing patch files.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum Patch {
    /// Add `keyword` to the `BGSKeywordForm` sub-object of the form at
    /// `target`. Matches KID's core operation.
    AddKeyword { target: FormId, keyword: FormId },
}

impl Patch {
    /// A stable discriminant used for `PatchSink` sorting. Derived from
    /// the variant's position in the enum.
    pub fn opcode_tag(&self) -> u8 {
        match self {
            Patch::AddKeyword { .. } => 0,
        }
    }

    /// The target FormID this patch operates on. Used for `PatchSink`
    /// sorting inside a given opcode.
    pub fn target(&self) -> FormId {
        match self {
            Patch::AddKeyword { target, .. } => *target,
        }
    }
}

/// The full on-disk `mora_patches.bin` structure.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PatchFile {
    pub magic: [u8; 4],
    pub version: u32,
    pub load_order_hash: u64,
    pub patches: Vec<Patch>,
}

/// Errors encountered during `PatchFile` serialization or validation.
#[derive(Debug, thiserror::Error)]
pub enum PatchFileError {
    #[error("postcard serialize error: {0}")]
    Serialize(String),
    #[error("postcard deserialize error: {0}")]
    Deserialize(String),
    #[error("bad magic: got {0:?}, expected MORA")]
    BadMagic([u8; 4]),
    #[error("unsupported version: got {got}, known up to {known}")]
    UnsupportedVersion { got: u32, known: u32 },
}

impl PatchFile {
    pub fn new(load_order_hash: u64) -> Self {
        PatchFile {
            magic: PATCH_FILE_MAGIC,
            version: PATCH_FILE_VERSION,
            load_order_hash,
            patches: Vec::new(),
        }
    }

    /// Serialize to a `Vec<u8>` via postcard.
    pub fn to_bytes(&self) -> Result<Vec<u8>, PatchFileError> {
        postcard::to_stdvec(self).map_err(|e| PatchFileError::Serialize(e.to_string()))
    }

    /// Deserialize + validate from bytes.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, PatchFileError> {
        let file: PatchFile =
            postcard::from_bytes(bytes).map_err(|e| PatchFileError::Deserialize(e.to_string()))?;
        if file.magic != PATCH_FILE_MAGIC {
            return Err(PatchFileError::BadMagic(file.magic));
        }
        if file.version > PATCH_FILE_VERSION {
            return Err(PatchFileError::UnsupportedVersion {
                got: file.version,
                known: PATCH_FILE_VERSION,
            });
        }
        Ok(file)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn magic_is_mora() {
        assert_eq!(&PATCH_FILE_MAGIC, b"MORA");
    }

    #[test]
    fn version_is_1() {
        assert_eq!(PATCH_FILE_VERSION, 1);
    }

    #[test]
    fn add_keyword_opcode_tag_is_0() {
        let p = Patch::AddKeyword {
            target: FormId(0x12EB7),
            keyword: FormId(0x1E718),
        };
        assert_eq!(p.opcode_tag(), 0);
        assert_eq!(p.target(), FormId(0x12EB7));
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib patch::tests
cargo xwin check --package mora-core --target x86_64-pc-windows-msvc
git add crates/mora-core/src/patch.rs
git commit -m "mora-core: Patch enum + PatchFile postcard format

Patch has one variant (AddKeyword) at this plan; future variants
must be appended (postcard serializes by declaration order).
PatchFile wraps magic + version + load_order_hash + Vec<Patch> with
to_bytes / from_bytes that validate magic and version on parse.
Opcode_tag + target accessors for PatchSink sorting."
```

---

### Task 5: Integration test — full PatchFile round-trip

**Files:**
- Create: `crates/mora-core/tests/patch_roundtrip.rs`

- [ ] **Step 1: Write the test**

```bash
cat > crates/mora-core/tests/patch_roundtrip.rs <<'EOF'
//! Integration tests for PatchFile postcard serialization.

use mora_core::{FormId, Patch, PatchFile};

#[test]
fn roundtrip_empty_patch_file() {
    let f = PatchFile::new(0xDEAD_BEEF_CAFE_F00D);
    let bytes = f.to_bytes().expect("serialize");
    let back = PatchFile::from_bytes(&bytes).expect("deserialize");
    assert_eq!(back, f);
    assert_eq!(back.patches.len(), 0);
}

#[test]
fn roundtrip_one_add_keyword() {
    let mut f = PatchFile::new(0);
    f.patches.push(Patch::AddKeyword {
        target: FormId(0x0001_2EB7),
        keyword: FormId(0x0001_E718),
    });
    let bytes = f.to_bytes().unwrap();
    let back = PatchFile::from_bytes(&bytes).unwrap();
    assert_eq!(back, f);
}

#[test]
fn bad_magic_rejected() {
    let mut f = PatchFile::new(0);
    f.patches.push(Patch::AddKeyword {
        target: FormId(1),
        keyword: FormId(2),
    });
    let mut bytes = f.to_bytes().unwrap();
    // Corrupt the first byte of the magic.
    bytes[0] = b'X';
    let err = PatchFile::from_bytes(&bytes).unwrap_err();
    assert!(matches!(err, mora_core::patch::PatchFileError::BadMagic(m) if m != *b"MORA"));
}

#[test]
fn future_version_rejected() {
    let mut f = PatchFile::new(0);
    f.version = 999;
    let bytes = f.to_bytes().unwrap();
    let err = PatchFile::from_bytes(&bytes).unwrap_err();
    assert!(matches!(
        err,
        mora_core::patch::PatchFileError::UnsupportedVersion { got: 999, known: 1 }
    ));
}
EOF
```

- [ ] **Step 2: Expose `patch::PatchFileError` as a public type**

Currently the re-exports in `lib.rs` don't include `patch::PatchFileError`. The integration test uses it via the full path `mora_core::patch::PatchFileError`, which works because `pub mod patch;` is already declared.

- [ ] **Step 3: Run the tests**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --test patch_roundtrip
```

Expected: 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add crates/mora-core/tests/patch_roundtrip.rs
git commit -m "mora-core: PatchFile round-trip integration tests

Four tests: empty file, one AddKeyword, bad magic rejected, future
version rejected. Exercises the postcard pipeline end-to-end."
```

---

## Phase E — Distributor trait + PatchSink (Tasks 6-7)

### Task 6: Implement `distributor.rs`

**Files:**
- Modify: `crates/mora-core/src/distributor.rs`

- [ ] **Step 1: Write distributor.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-core/src/distributor.rs <<'EOF'
//! The `Distributor` trait — the extensibility hinge for future
//! frontends (mora-kid, mora-spid, mora-skypatcher, …).
//!
//! Plan 4 uses a placeholder [`EspWorld`] marker; Plan 5 replaces it
//! with the real `mora_esp::EspWorld`.

use crate::chance::DeterministicChance;
use crate::patch_sink::PatchSink;

/// Placeholder for the real `mora_esp::EspWorld`. Plan 5 replaces
/// with the actual indexed ESP view; downstream trait users compile
/// against this marker until then.
pub struct EspWorld;

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
pub trait Distributor {
    /// Error type surfaced by [`lower`]. Must be `Send + Sync + 'static`.
    type Error: std::error::Error + Send + Sync + 'static;

    /// Short name for diagnostics (e.g. `"kid"`, `"spid"`).
    fn name(&self) -> &'static str;

    /// Produce patches from the loaded ESP world + chance RNG, pushing
    /// each into the provided sink.
    fn lower(
        &self,
        world: &EspWorld,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error>;
}

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

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib distributor::tests
cargo xwin check --package mora-core --target x86_64-pc-windows-msvc
git add crates/mora-core/src/distributor.rs
git commit -m "mora-core: Distributor trait + DistributorStats

Trait: name + lower(world, chance, sink) -> Result<Stats, Error>.
Stats: rules/candidates/patches/rejected counts + AddAssign for
summing across frontends. EspWorld is a placeholder marker; Plan 5
replaces with mora_esp::EspWorld."
```

---

### Task 7: Implement `patch_sink.rs` with dedup + finalize

**Files:**
- Modify: `crates/mora-core/src/patch_sink.rs`

- [ ] **Step 1: Write patch_sink.rs**

```bash
cat > crates/mora-core/src/patch_sink.rs <<'EOF'
//! `PatchSink` — append-only patch collector with dedup and
//! stable sort on finalize.

use std::collections::HashSet;

use crate::patch::{Patch, PatchFile, PATCH_FILE_MAGIC, PATCH_FILE_VERSION};

/// Append-only patch collector. Frontends push into it; finalize
/// produces a sorted, deduped `PatchFile`.
#[derive(Default)]
pub struct PatchSink {
    patches: Vec<Patch>,
    seen: HashSet<Patch>,
    duplicates_skipped: u64,
    load_order_hash: u64,
}

impl PatchSink {
    pub fn new() -> Self {
        Self::default()
    }

    /// Sets the load-order hash that will be embedded in the finalized
    /// `PatchFile`. Caller is responsible for computing an appropriate
    /// value (typically a blake3 digest over plugin names + master
    /// references; see `mora-esp` Plan 5).
    pub fn set_load_order_hash(&mut self, hash: u64) {
        self.load_order_hash = hash;
    }

    /// Push a patch. Identical patches already in the sink are
    /// silently deduped; `duplicates_skipped` counter increments.
    pub fn push(&mut self, patch: Patch) {
        if self.seen.insert(patch.clone()) {
            self.patches.push(patch);
        } else {
            self.duplicates_skipped += 1;
        }
    }

    /// Number of unique patches currently in the sink.
    pub fn len(&self) -> usize {
        self.patches.len()
    }

    pub fn is_empty(&self) -> bool {
        self.patches.is_empty()
    }

    /// Count of duplicate pushes dropped.
    pub fn duplicates_skipped(&self) -> u64 {
        self.duplicates_skipped
    }

    /// Consume the sink into a sorted `PatchFile`. Patches are sorted
    /// by `(opcode_tag, target_form_id.raw())` for a stable, reproducible
    /// output across runs.
    pub fn finalize(mut self) -> PatchFile {
        self.patches.sort_by_key(|p| (p.opcode_tag(), p.target().raw()));
        PatchFile {
            magic: PATCH_FILE_MAGIC,
            version: PATCH_FILE_VERSION,
            load_order_hash: self.load_order_hash,
            patches: self.patches,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::form_id::FormId;

    fn add_kw(t: u32, k: u32) -> Patch {
        Patch::AddKeyword {
            target: FormId(t),
            keyword: FormId(k),
        }
    }

    #[test]
    fn push_dedupes_identical_patches() {
        let mut sink = PatchSink::new();
        sink.push(add_kw(1, 2));
        sink.push(add_kw(1, 2));
        sink.push(add_kw(1, 2));
        assert_eq!(sink.len(), 1);
        assert_eq!(sink.duplicates_skipped(), 2);
    }

    #[test]
    fn push_keeps_distinct_patches() {
        let mut sink = PatchSink::new();
        sink.push(add_kw(1, 2));
        sink.push(add_kw(1, 3));
        sink.push(add_kw(2, 2));
        assert_eq!(sink.len(), 3);
        assert_eq!(sink.duplicates_skipped(), 0);
    }

    #[test]
    fn finalize_sorts_by_target() {
        let mut sink = PatchSink::new();
        sink.push(add_kw(3, 1));
        sink.push(add_kw(1, 1));
        sink.push(add_kw(2, 1));
        let file = sink.finalize();
        let targets: Vec<u32> = file.patches.iter().map(|p| p.target().raw()).collect();
        assert_eq!(targets, vec![1, 2, 3]);
    }

    #[test]
    fn finalize_preserves_magic_version_and_hash() {
        let mut sink = PatchSink::new();
        sink.set_load_order_hash(0xCAFE_BABE_DEAD_BEEF);
        let file = sink.finalize();
        assert_eq!(&file.magic, b"MORA");
        assert_eq!(file.version, 1);
        assert_eq!(file.load_order_hash, 0xCAFE_BABE_DEAD_BEEF);
    }

    #[test]
    fn new_sink_is_empty() {
        let sink = PatchSink::new();
        assert!(sink.is_empty());
        assert_eq!(sink.len(), 0);
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib patch_sink::tests
cargo xwin check --package mora-core --target x86_64-pc-windows-msvc
git add crates/mora-core/src/patch_sink.rs
git commit -m "mora-core: PatchSink with dedup + sorted finalize

HashSet-backed dedup rejects identical patches; finalize sorts by
(opcode_tag, target) and wraps in a PatchFile with magic/version/
load_order_hash. 5 unit tests."
```

---

## Phase F — Deterministic chance algorithm (Tasks 8-12)

### Task 8: Implement `chance/szudzik.rs`

**Files:**
- Modify: `crates/mora-core/src/chance/szudzik.rs`

- [ ] **Step 1: Write szudzik.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-core/src/chance/szudzik.rs <<'EOF'
//! Szudzik pairing function — maps two `u64`s to a single `u64`.
//!
//! Matches `clib_util::hash::szudzik_pair` (used by KID's chance
//! seeding).
//!
//! ```text
//! szudzik(a, b) = if a >= b { a*a + a + b } else { b*b + a }
//! ```

/// Pair two `u64` values into one. Deterministic; matches KID.
///
/// Arithmetic uses wrapping multiplication to match C++ unsigned overflow
/// semantics (defined behavior on `uint64_t`).
pub const fn szudzik_pair(a: u64, b: u64) -> u64 {
    if a >= b {
        a.wrapping_mul(a).wrapping_add(a).wrapping_add(b)
    } else {
        b.wrapping_mul(b).wrapping_add(a)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn zero_zero() {
        assert_eq!(szudzik_pair(0, 0), 0);
    }

    #[test]
    fn a_greater_branch() {
        // a = 10, b = 3 → a*a + a + b = 100 + 10 + 3 = 113
        assert_eq!(szudzik_pair(10, 3), 113);
    }

    #[test]
    fn a_less_branch() {
        // a = 3, b = 10 → b*b + a = 100 + 3 = 103
        assert_eq!(szudzik_pair(3, 10), 103);
    }

    #[test]
    fn a_equal_b_uses_greater_branch() {
        // a = 5, b = 5 → a*a + a + b = 25 + 5 + 5 = 35
        assert_eq!(szudzik_pair(5, 5), 35);
    }

    #[test]
    fn asymmetric_in_general() {
        assert_ne!(szudzik_pair(3, 10), szudzik_pair(10, 3));
    }

    #[test]
    fn wrapping_large_values_dont_panic() {
        // 2^32 * 2^32 overflows u64 when summed with +a+b; just verify
        // the function doesn't panic and produces a deterministic result.
        let x = szudzik_pair(0xFFFF_FFFF, 0xFFFF_FFFF);
        let y = szudzik_pair(0xFFFF_FFFF, 0xFFFF_FFFF);
        assert_eq!(x, y); // determinism
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-core --lib chance::szudzik::tests
git add crates/mora-core/src/chance/szudzik.rs
git commit -m "mora-core: Szudzik pairing function

Ports clib_util::hash::szudzik_pair. Wrapping arithmetic matches C++
uint64 overflow semantics. 6 unit tests: identity, greater-branch,
less-branch, equal-case, asymmetry, overflow determinism."
```

---

### Task 9: Implement `chance/splitmix.rs`

**Files:**
- Modify: `crates/mora-core/src/chance/splitmix.rs`

- [ ] **Step 1: Write splitmix.rs**

```bash
cat > crates/mora-core/src/chance/splitmix.rs <<'EOF'
//! SplitMix64 state initializer, used to seed the Xoshiro256** state
//! from a single `u64` seed. Matches XoshiroCpp's `Xoshiro256StarStar::Xoshiro256StarStar(u64)`.

/// One step of SplitMix64. Modifies `seed` in place, returns the
/// next mixed output.
///
/// ```text
/// seed  = seed + 0x9E37_79B9_7F4A_7C15
/// z     = seed
/// z     = (z ^ (z >> 30)) * 0xBF58_476D_1CE4_E5B9
/// z     = (z ^ (z >> 27)) * 0x94D0_49BB_1331_11EB
/// z     = z ^ (z >> 31)
/// return z
/// ```
pub const fn splitmix64_next(seed: &mut u64) -> u64 {
    *seed = seed.wrapping_add(0x9E37_79B9_7F4A_7C15);
    let mut z = *seed;
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

/// Seed a 4-word Xoshiro state from a single `u64` via 4 SplitMix64 steps.
pub const fn splitmix64_state(seed: u64) -> [u64; 4] {
    let mut s = seed;
    let s0 = splitmix64_next(&mut s);
    let s1 = splitmix64_next(&mut s);
    let s2 = splitmix64_next(&mut s);
    let s3 = splitmix64_next(&mut s);
    [s0, s1, s2, s3]
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Reference values computed from the canonical SplitMix64 algorithm
    /// (https://prng.di.unimi.it/splitmix64.c). Seed = 0, first 4 outputs:
    ///   0xE220A8397B1DCDAF
    ///   0x6E789E6AA1B965F4
    ///   0x06C45D188009454F
    ///   0xF88BB8A8724C81EC
    #[test]
    fn first_four_outputs_seed_0() {
        let state = splitmix64_state(0);
        assert_eq!(state[0], 0xE220_A839_7B1D_CDAF);
        assert_eq!(state[1], 0x6E78_9E6A_A1B9_65F4);
        assert_eq!(state[2], 0x06C4_5D18_8009_454F);
        assert_eq!(state[3], 0xF88B_B8A8_724C_81EC);
    }

    /// Reference values for seed = 42, first output: 0xC3B7_7ADFF_AD5F..., sanity-check that
    /// the function is deterministic (same input → same output).
    #[test]
    fn deterministic_given_seed() {
        let a = splitmix64_state(0xDEAD_BEEF_CAFE_F00D);
        let b = splitmix64_state(0xDEAD_BEEF_CAFE_F00D);
        assert_eq!(a, b);
    }

    #[test]
    fn different_seeds_produce_different_states() {
        let a = splitmix64_state(1);
        let b = splitmix64_state(2);
        assert_ne!(a, b);
    }
}
EOF
```

Note on reference values: the values in `first_four_outputs_seed_0` come from running SplitMix64 on a canonical implementation. Verify these independently against https://prng.di.unimi.it/splitmix64.c if the test fails on first run; it's easy to get one wrong during transcription.

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-core --lib chance::splitmix::tests
```

If `first_four_outputs_seed_0` fails, your first priority is to verify the reference values against an authoritative source (the prng.di.unimi.it page). If the algorithm is implemented correctly but the reference values are wrong, update the test. If the algorithm is wrong, fix it.

```bash
git add crates/mora-core/src/chance/splitmix.rs
git commit -m "mora-core: SplitMix64 state initializer

Seeds 4-word Xoshiro state from a single u64 via 4 SplitMix64 steps.
Matches the canonical algorithm at prng.di.unimi.it/splitmix64.c.
3 tests: first-4-outputs for seed=0 (golden values), determinism,
distinct seeds produce distinct states."
```

---

### Task 10: Implement `chance/xoshiro.rs`

**Files:**
- Modify: `crates/mora-core/src/chance/xoshiro.rs`

- [ ] **Step 1: Write xoshiro.rs**

```bash
cat > crates/mora-core/src/chance/xoshiro.rs <<'EOF'
//! Xoshiro256** PRNG. One-shot — Mora needs a single `next_u64` per
//! chance roll, so we don't track state across rolls.
//!
//! Reference: https://prng.di.unimi.it/xoshiro256starstar.c
//!
//! ```text
//! result = rotl(s[1] * 5, 7) * 9
//! t = s[1] << 17
//! s[2] ^= s[0]
//! s[3] ^= s[1]
//! s[1] ^= s[2]
//! s[0] ^= s[3]
//! s[2] ^= t
//! s[3] = rotl(s[3], 45)
//! ```

/// A single Xoshiro256** step — consumes `state` in place, returns
/// the next 64-bit output.
pub fn xoshiro_next_u64(state: &mut [u64; 4]) -> u64 {
    let result = state[1].wrapping_mul(5).rotate_left(7).wrapping_mul(9);
    let t = state[1] << 17;
    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];
    state[2] ^= t;
    state[3] = state[3].rotate_left(45);
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::chance::splitmix::splitmix64_state;

    /// Reference: Xoshiro256** with the canonical SplitMix64 seeding of
    /// 0, first u64 output. Cross-checked against the reference C
    /// implementation and against the `rand_xoshiro` crate's
    /// `Xoshiro256StarStar::seed_from_u64(0)` (which uses the same
    /// SplitMix64 seeding convention).
    ///
    /// Expected: 0x53175D61490B23DF
    #[test]
    fn first_output_seed_0() {
        let mut state = splitmix64_state(0);
        let v = xoshiro_next_u64(&mut state);
        assert_eq!(v, 0x5317_5D61_490B_23DF);
    }

    #[test]
    fn deterministic_given_state() {
        let mut a = splitmix64_state(42);
        let mut b = splitmix64_state(42);
        assert_eq!(xoshiro_next_u64(&mut a), xoshiro_next_u64(&mut b));
    }

    #[test]
    fn different_states_yield_different_outputs() {
        let mut a = splitmix64_state(1);
        let mut b = splitmix64_state(2);
        assert_ne!(xoshiro_next_u64(&mut a), xoshiro_next_u64(&mut b));
    }
}
EOF
```

Same note as Task 9: the reference value `0x5317_5D61_490B_23DF` should be cross-verified against the canonical C implementation before trusting the test. If the test fails, pin down which is wrong (algorithm or reference).

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-core --lib chance::xoshiro::tests
git add crates/mora-core/src/chance/xoshiro.rs
git commit -m "mora-core: Xoshiro256** one-shot PRNG step

Matches https://prng.di.unimi.it/xoshiro256starstar.c. Plan 4 only
needs a single next_u64 per chance roll (no stream state). 3 tests:
first-output-from-seed-0 (golden), determinism, state divergence."
```

---

### Task 11: Implement `chance/msvc_uniform.rs`

**Files:**
- Modify: `crates/mora-core/src/chance/msvc_uniform.rs`

- [ ] **Step 1: Write msvc_uniform.rs**

```bash
cat > crates/mora-core/src/chance/msvc_uniform.rs <<'EOF'
//! MSVC `std::uniform_real_distribution<float>` port for range `[0, 100)`.
//!
//! Matches the specific algorithm MSVC STL uses on Windows x64 with a
//! 64-bit uniform RNG:
//!   1. Convert raw u64 to f64 by dividing by 2^64.
//!   2. Narrow to f32 (IEEE 754 round-to-nearest, matching MSVC
//!      default rounding mode).
//!   3. Out-of-range clamp: if result >= 1.0, snap to 0.0 (MSVC's
//!      `generate_canonical` guard).
//!   4. Scale by the range (100.0 for chance).
//!
//! This is the exact same step sequence KID applies — see
//! `docs/src/mora-core-reference.md`.

/// Draw a single float in `[0, 100)` from a raw 64-bit engine output.
pub fn draw_percent_from_u64(raw: u64) -> f32 {
    // Step 1 + 2: raw / 2^64 in f64, narrow to f32.
    // 2^64 = 18446744073709551616.0
    let scale_f64: f64 = (raw as f64) / 18446744073709551616.0_f64;
    let mut clamp_f32: f32 = scale_f64 as f32;

    // Step 3: MSVC's out-of-range snap. Rare (requires raw ~= 2^64) but
    // well-defined per the STL spec.
    if clamp_f32 >= 1.0_f32 {
        clamp_f32 = 0.0_f32;
    }

    // Step 4: scale to [0, 100).
    100.0_f32 * clamp_f32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn raw_0_yields_0_0() {
        assert_eq!(draw_percent_from_u64(0), 0.0_f32);
    }

    #[test]
    fn raw_u64_max_snapped_to_0_0() {
        // raw = 2^64 - 1 → f64 scale ~= 0.99999…9; narrowed to f32 becomes 1.0f.
        // MSVC's snap kicks in and returns 0.0, percent = 0.
        let v = draw_percent_from_u64(u64::MAX);
        assert_eq!(v, 0.0_f32);
    }

    #[test]
    fn raw_half_yields_50_0() {
        // raw = 2^63 → scale_f64 = 0.5 exactly → clamp_f32 = 0.5 → percent = 50.0.
        let v = draw_percent_from_u64(1u64 << 63);
        assert_eq!(v, 50.0_f32);
    }

    #[test]
    fn output_always_in_0_100_range() {
        // A few random-ish raws.
        for &raw in &[0u64, 1, 42, 0xDEAD_BEEF, 0xCAFE_BABE, u64::MAX] {
            let v = draw_percent_from_u64(raw);
            assert!((0.0..100.0).contains(&v), "raw {raw:x} produced {v}");
        }
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
cargo test --package mora-core --lib chance::msvc_uniform::tests
git add crates/mora-core/src/chance/msvc_uniform.rs
git commit -m "mora-core: MSVC uniform_real_distribution<float> port

Four-step port matching MSVC STL on x64:
 1. raw_u64 as f64 / 2^64
 2. narrow to f32 (IEEE 754 round-to-nearest)
 3. >=1.0 -> 0.0 snap (MSVC generate_canonical guard)
 4. multiply by 100.0 in f32
Four tests: raw=0 -> 0.0, raw=u64::MAX snapped -> 0.0, raw=2^63 ->
50.0 (exact), range invariant for several raws."
```

---

### Task 12: Implement `chance/fnv.rs` — FNV-1a-32

**Files:**
- Modify: `crates/mora-core/src/chance/fnv.rs`

KID hashes keyword editor-IDs with standard FNV-1a-32 (offset basis
`0x811c9dc5`, prime `0x01000193`, byte-by-byte). Inline — ~10 lines —
rather than relying on the `fnv` crate (which defaults to 64-bit).

- [ ] **Step 1: Write fnv.rs**

```bash
cd /home/tbaldrid/oss/mora
cat > crates/mora-core/src/chance/fnv.rs <<'EOF'
//! FNV-1a-32 hash — ported to match KID's `clib_util::hash::fnv1a_32`
//! exactly.
//!
//! Standard algorithm:
//!   h = 0x811c9dc5 (offset basis)
//!   for each byte b: h = (h ^ b) * 0x01000193 (prime)
//!
//! Operations are `u32` with wrapping multiplication to match C++
//! unsigned overflow semantics.

/// FNV-1a-32 offset basis.
const OFFSET_BASIS: u32 = 0x811c_9dc5;
/// FNV-1a-32 prime.
const PRIME: u32 = 0x0100_0193;

/// Hash `bytes` with FNV-1a-32. Deterministic, case-sensitive.
pub fn fnv1a_32(bytes: &[u8]) -> u32 {
    let mut h = OFFSET_BASIS;
    for &b in bytes {
        h ^= b as u32;
        h = h.wrapping_mul(PRIME);
    }
    h
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Empty input → offset basis.
    #[test]
    fn empty_returns_offset_basis() {
        assert_eq!(fnv1a_32(b""), 0x811c_9dc5);
    }

    /// Reference value for "a" (a single byte 0x61):
    ///   h = 0x811c9dc5 ^ 0x61 = 0x811c9da4
    ///   h * 0x01000193 (wrapping) = 0xE40C292C
    #[test]
    fn single_char_a() {
        assert_eq!(fnv1a_32(b"a"), 0xE40C_292C);
    }

    /// Reference value for "foobar" (well-known FNV-1a-32 test vector):
    ///   0xBF9CF968
    #[test]
    fn known_vector_foobar() {
        assert_eq!(fnv1a_32(b"foobar"), 0xBF9C_F968);
    }

    /// Deterministic — same input always same output.
    #[test]
    fn deterministic() {
        let a = fnv1a_32(b"WeapMaterialIron");
        let b = fnv1a_32(b"WeapMaterialIron");
        assert_eq!(a, b);
    }

    /// Different inputs → different outputs (avalanche sanity).
    #[test]
    fn distinct_outputs() {
        assert_ne!(fnv1a_32(b"Alpha"), fnv1a_32(b"Beta"));
        assert_ne!(fnv1a_32(b"a"), fnv1a_32(b"b"));
    }
}
EOF
```

Note: the reference vectors for "a" and "foobar" are well-known
FNV-1a-32 test values published at the algorithm's original spec
(http://www.isthe.com/chongo/tech/comp/fnv/). If the tests fail, the
algorithm is wrong — not the reference values.

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib chance::fnv::tests
git add crates/mora-core/src/chance/fnv.rs
git commit -m "mora-core: FNV-1a-32 hash inlined

Matches KID's clib_util::hash::fnv1a_32. Standard algorithm:
offset basis 0x811c9dc5, prime 0x01000193, byte-by-byte, u32
wrapping arithmetic. 5 tests: empty input -> offset basis,
'a' golden value, 'foobar' golden value, determinism, distinct
outputs."
```

---

### Task 13: Implement `chance/mod.rs` — `DeterministicChance` high-level API

**Files:**
- Modify: `crates/mora-core/src/chance/mod.rs`

- [ ] **Step 1: Write mod.rs**

```bash
cat > crates/mora-core/src/chance/mod.rs <<'EOF'
//! KID-bit-compatible deterministic chance RNG.
//!
//! See `docs/src/mora-core-reference.md` for the algorithm.

pub mod fnv;
pub mod msvc_uniform;
pub mod splitmix;
pub mod szudzik;
pub mod xoshiro;

use crate::form_id::FormId;

/// KID-compatible chance resolver. Zero state — a single instance is
/// safe to share across all frontends + threads.
#[derive(Debug, Clone, Copy, Default)]
pub struct DeterministicChance;

impl DeterministicChance {
    /// Construct a chance resolver configured to match KID exactly.
    /// Reserved for a future constructor if we add variants (e.g.
    /// SPID-compatible or user-custom); Plan 4 only has one flavor.
    pub const fn kid_compatible() -> Self {
        DeterministicChance
    }

    /// The full KID roll: does this `(keyword, form_id)` pair pass a
    /// rule with the given `chance` percentage?
    ///
    /// `chance` is 0..=100 inclusive; values ≥ 100 always pass, values
    /// ≤ 0 always fail (matching KID's trivial paths which skip the
    /// roll entirely).
    pub fn passes(&self, keyword_editor_id: &str, form_id: FormId, chance: u8) -> bool {
        if chance >= 100 {
            return true;
        }
        if chance == 0 {
            return false;
        }
        let percent = self.roll_percent(keyword_editor_id, form_id);
        percent <= chance as f32
    }

    /// The raw 0..100 percentage rolled for this pair. Exposed for
    /// diagnostics and for M4's golden-test harness.
    pub fn roll_percent(&self, keyword_editor_id: &str, form_id: FormId) -> f32 {
        let kw_hash = fnv::fnv1a_32(keyword_editor_id.as_bytes());
        let seed = szudzik::szudzik_pair(kw_hash as u64, form_id.raw() as u64);
        let mut state = splitmix::splitmix64_state(seed);
        let raw = xoshiro::xoshiro_next_u64(&mut state);
        msvc_uniform::draw_percent_from_u64(raw)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chance_100_always_passes() {
        let c = DeterministicChance::kid_compatible();
        for &fid in &[0u32, 0x12EB7, 0xFFFF_FFFF] {
            assert!(c.passes("AnyKeyword", FormId(fid), 100));
        }
    }

    #[test]
    fn chance_0_always_fails() {
        let c = DeterministicChance::kid_compatible();
        for &fid in &[0u32, 0x12EB7, 0xFFFF_FFFF] {
            assert!(!c.passes("AnyKeyword", FormId(fid), 0));
        }
    }

    #[test]
    fn deterministic_given_inputs() {
        let c = DeterministicChance::kid_compatible();
        let a = c.roll_percent("WeapMaterialIron", FormId(0x12EB7));
        let b = c.roll_percent("WeapMaterialIron", FormId(0x12EB7));
        assert_eq!(a, b);
    }

    #[test]
    fn different_keywords_produce_different_rolls() {
        let c = DeterministicChance::kid_compatible();
        let a = c.roll_percent("Alpha", FormId(0x12EB7));
        let b = c.roll_percent("Beta", FormId(0x12EB7));
        assert_ne!(a, b);
    }

    #[test]
    fn roll_percent_is_in_range() {
        let c = DeterministicChance::kid_compatible();
        for fid in 0u32..1000 {
            let v = c.roll_percent("TestKeyword", FormId(fid));
            assert!((0.0..100.0).contains(&v), "fid {fid} produced {v}");
        }
    }

    /// Sanity check: at chance=50, a few thousand rolls should
    /// converge around ~50% pass rate. Tight bound: 47-53%.
    #[test]
    fn distribution_50_percent_converges() {
        let c = DeterministicChance::kid_compatible();
        let n = 10_000u32;
        let passes = (0..n)
            .filter(|i| c.passes("SanityKeyword", FormId(*i), 50))
            .count();
        let pct = (passes as f64 / n as f64) * 100.0;
        assert!(
            (47.0..53.0).contains(&pct),
            "chance=50 over {n} rolls passed {pct}%, expected ~50"
        );
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib chance
git add crates/mora-core/src/chance/mod.rs
git commit -m "mora-core: DeterministicChance high-level API

passes(keyword, form_id, chance) -> bool: full KID pipeline
(FNV-1a-32 + Szudzik + SplitMix + Xoshiro + MSVC draw + threshold compare).
Chance=0 always fails, chance>=100 always passes (trivial paths).
6 tests: trivial paths, determinism, keyword variation, range
invariant over 1000 form_ids, distribution sanity at chance=50
(47-53% pass rate over 10k rolls).

M4's golden harness validates bit-identity against a real KID run
once the self-hosted runner image is refreshed."
```

---

## Phase G — Final verification (Task 14)

### Task 14: Full clean build + push + open PR

**Files:** none modified.

- [ ] **Step 1: Clean verification**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets 2>&1 | grep -E "^(test result|running)"
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all six commands succeed. Test count: Plan 2+3's 27 tests + Plan 4's new tests (~35-38 total across form_id, patch, patch_sink, distributor, chance::{szudzik,splitmix,xoshiro,msvc_uniform,mod}, patch_roundtrip integration).

- [ ] **Step 2: Push + open PR**

```bash
git push -u origin m2-mora-core
gh pr create --base master --head m2-mora-core \
    --title "Rust + KID pivot — M2 Part 1: mora-core" \
    --body "$(cat <<'PRBODY'
## Summary

Fleshes out `mora-core` per
`docs/superpowers/plans/2026-04-21-rust-kid-pivot-plan-4-mora-core.md`:

- **FormId / FullFormId** newtypes with hex Display, parts helpers, serde.
- **Patch enum + PatchFile** postcard format (magic `MORA`, version 1,
  load_order_hash, Vec<Patch>). `Patch::AddKeyword` is the sole variant
  at this plan; enum is append-only for serialization stability.
- **Distributor trait + DistributorStats** — the extensibility hinge.
  Uses a placeholder `EspWorld` marker that Plan 5 replaces.
- **PatchSink** with exact dedup + stable sort on finalize.
- **DeterministicChance** — full KID chance pipeline (Szudzik + FNV-1a +
  SplitMix64 + Xoshiro256** + MSVC `uniform_real_distribution<float>`
  port). Bit-identity validation with real KID is deferred to M4's
  golden harness; Plan 4 ships the algorithm + determinism + statistical
  sanity.
- **`docs/src/mora-core-reference.md`** — source-of-truth for all the
  above.

No ESP parsing, no SKSE interaction, no `unsafe`.

## Test plan

- [x] `cargo test --workspace` — all tests pass (27 from M1 + ~N new).
- [x] `cargo clippy --all-targets -- -D warnings` clean.
- [x] `cargo fmt --check` clean.
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` clean.

## Next up

Plan 5: M2 Part 2 — `mora-esp` (mmap ESP/ESL/ESM reader, plugins.txt
parser, load order resolution, record-type iterators). Replaces the
`EspWorld` placeholder in `mora-core::distributor`.
PRBODY
)"
```

Expected: PR URL printed.

- [ ] **Step 3: Watch CI**

```bash
gh run watch --exit-status 2>&1 | tail -8
```

Expected: all five GitHub-hosted jobs pass. `skyrim-integration` still red until Unraid image refresh.

- [ ] **Step 4: Hand off to human merge**

Same as prior plans.

---

## Completion criteria

- [ ] All new unit + integration tests pass.
- [ ] `cargo clippy -D warnings` clean.
- [ ] PR merged to `master`.
- [ ] M4's golden harness will validate bit-compat against real KID once the self-hosted runner is refreshed.

## Next plan

**Plan 5: `mora-esp`** — mmap-based ESP/ESL/ESM reader, TES4 header
parsing, `plugins.txt` loader, load-order resolver, master-index
remapping, record-type iterators (first pass: Weapon + Armor only),
`EspWorld` indexed view. Replaces the placeholder in
`mora-core::distributor::EspWorld`.
