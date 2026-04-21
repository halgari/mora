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

