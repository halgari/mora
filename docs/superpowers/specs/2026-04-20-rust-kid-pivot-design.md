# Mora v0.1 — Rust + KID Pivot — Design Spec

**Date:** 2026-04-20
**Status:** Draft — approved in brainstorming, pending written review

## Problem

Mora today is a custom Datalog-style language for Skyrim SE, with a C++20/C++23 compiler, static evaluator, differential-dataflow runtime, LSP server, and VS Code extension. The language is the most novel part of the project and also the largest maintenance burden. Meanwhile, the subset of the project that has concrete near-term value — reading ESP load orders and producing a precomputed keyword-distribution patch file — overlaps closely with an existing, popular SKSE plugin: the **Keyword Item Distributor (KID)** by powerof3.

Two problems drive this pivot:

1. **Scope dilution.** Every feature added to the language (parser, semantic analyzer, LSP, VS Code extension, datalog evaluator, dataflow engine) is time not spent on the ESP + distribution + runtime core. The language is optional; the core is not.
2. **Toolchain pain.** The C++ build is cross-compiled from Linux to Windows via clang-cl + xwin + xmake, with its own xmake-to-compile_commands bridge, clang-tidy sweep, and custom clang-cl toolchain definitions. Adequate, but heavy — every new file is a cost to the maintainer.

## Goal

A small, fast, Rust-native tool that is a **drop-in replacement for KID's INI files**: point it at a Skyrim install, and it produces a patch file that, loaded by Mora's runtime DLL, yields the exact same keyword distribution KID would yield in-game — without KID itself installed, without a plugin slot, and with all filter evaluation + chance rolling precomputed at compile time.

Secondary goals:

- **Slick Rust API.** Library-first design. The CLI is one frontend; the Rust API is the primary artifact. Other tools (editors, mod managers, automation) should be able to link `mora-kid` directly and get the same results.
- **Segmented for future distributors.** v1 is KID-only. The architecture admits future `mora-spid`, `mora-skypatcher`, and similar frontends as additional crates implementing a shared trait, with zero changes to the core.
- **Publish `skse-rs` as a reusable artifact.** The Rust SKSE framework we build for Mora's runtime is its own crate with its own release cadence, usable by other Rust-SKSE projects.

## Non-goals

- **No custom language.** No parser, no AST, no type system, no LSP, no VS Code extension. Everything in the existing `src/{lexer,ast,parser,sema,lsp}` tree is deleted.
- **No datalog / differential dataflow.** Everything `src/{dag,eval,emit}` builds is deleted. Distribution is fully precomputed; the runtime applies a flat list.
- **No dynamic `maintain` / `on` rules.** Mora v0.1 is 100% static distribution. Dynamic behavior can be revisited later.
- **No ESP output.** Mora never emits a `.esp`. Output is `mora_patches.bin` (postcard-serialized Rust enum), applied at `kDataLoaded`.
- **No MCM / in-game UI.** Runtime logs to SKSE log, that's it.
- **No save-game persistence.** Patches apply at each game load; they do not modify save files.

## Architecture

### Pipeline

```
  plugins.txt  ──┐
  Data/*.esp   ──┼─► [mora-esp]          → EspWorld (mmapped, indexed by record type)
  Data/*.esl   ──┘      load order, master resolution, subrecord iteration

  *_KID.ini    ────► [mora-kid::parse]   → Vec<KidRule> (AST with spans)

  EspWorld + KidRules
           │
           ▼
  [mora-kid impls Distributor::lower]
    - scan record-type-indexed candidates once per type
    - evaluate form/string/keyword/trait filters
    - deterministic chance roll per (rule × candidate), KID-bit-identical
    - emit Patch records into sink
           │
           ▼
  PatchSink ──► [mora-core::emit] ──► mora_patches.bin (postcard)

  ┌──────────────────────── runtime ─────────────────────────┐
  │  MoraRuntime.dll (SKSE, kDataLoaded)                     │
  │    mmap mora_patches.bin → deserialize PatchFile         │
  │    verify magic + version + load_order_hash              │
  │    for each Patch: apply via skse-rs bindings            │
  │    log summary to SKSE log                               │
  └──────────────────────────────────────────────────────────┘
```

### Key properties

- **Compile-time everything.** Chance, filters, traits, plugin resolution — all resolve statically against the user's live load order.
- **Patch file is ephemeral.** Regenerated whenever `plugins.txt` or any `*_KID.ini` changes. `load_order_hash` in the header sanity-checks the binding.
- **Runtime is dumb.** No KID grammar in the runtime. No RNG. No ESP parsing. Just FormID lookup + `AddKeyword`.
- **Frontend-pluggable.** `mora-kid` implements a `Distributor` trait in `mora-core`. Future frontends (`mora-spid`, `mora-skypatcher`) plug in the same way and coexist in the same compile.

## Workspace Layout

Cargo workspace at the repo root:

```
mora/
  Cargo.toml                    # workspace
  crates/
    skse-rs/                    # published separately to crates.io
    mora-core/                  # shared types, patch format, chance RNG, Distributor trait
    mora-esp/                   # mmap ESP/ESL/ESM reader, load order, plugins.txt
    mora-kid/                   # KID INI parser + rule → patch lowering
    mora-cli/                   # `mora` binary
    mora-runtime/               # SKSE DLL (cdylib)
    mora-test-harness/          # SKSE test-harness DLL (cdylib)
    xtask/                      # dev-workflow orchestration (capture-kid-goldens, stage-runner-image, etc.)
```

Integration scenarios are bash scripts under `tests/integration/<case>/` driven by CI matrix + the existing `run-skyrim-test.sh` orchestrator. There is no separate Rust-side integration test runner crate — the tests are shell hooks against the TCP harness protocol, which is already proven and language-agnostic.

### Dependency direction

Left depends on right; no cycles.

```
mora-cli ─► mora-kid ─► mora-core ◄─ mora-esp
              │                       ▲
              └───────────────────────┘

mora-runtime       ─► mora-core, skse-rs
mora-test-harness  ─► mora-core, skse-rs
xtask              ─► mora-cli (for CLI orchestration in dev workflows)
```

### Per-crate responsibilities

**`skse-rs`** *(name provisional — `mora-skse` is an alternative if we want Mora-adjacent naming)* — clean-room Rust reimplementation of the SKSE plugin infrastructure Mora needs. **Not** a binding to or wrapper around the C++ CommonLibSSE-NG library. Pure Rust, talks directly to the Skyrim process via:

- Address-library CSV/bin loading (SE/AE/VR target support).
- `#[repr(C)]` definitions for the game types we touch (`TESForm`, `BGSKeywordForm`, `TESDataHandler`, relevant subrecords).
- Explicit vtable dispatch via function-pointer tables.
- SKSE `SKSEPlugin_Query` / `SKSEPlugin_Load` entry points written as `#[no_mangle] extern "C" fn`.
- Messaging interface (registering for `kDataLoaded`).
- Logging glue to the SKSE log.

Every struct/function gets a lineage comment pointing to the CommonLibSSE-NG equivalent for cross-reference. We are reimplementing, not copying code. Published to crates.io as `skse-rs` 0.1.

**`mora-core`** — `Patch` enum, `PatchFile` struct (postcard-serializable), `FormId` / `FullFormId` newtypes, `Distributor` trait, `DeterministicChance` algorithm, `PatchSink` (dedupe + merge). No I/O. Fully unit-tested in isolation.

**`mora-esp`** — mmap-backed ESP/ESL/ESM parser. Exposes `EspWorld`: load a directory + plugins.txt, get an indexed view with record iterators (`world.records::<Weapon>()`, `world.record_by_fid(fid)`), master resolution, editor IDs, keywords, trait subrecord access. Zero-copy where possible — returned record views are `&` into the mmap. Parallel load via rayon. Fully custom implementation (no `esplugin` dependency); optimized for scan speed over hundreds of plugins.

**`mora-kid`** — parses `*_KID.ini` files with KID's exact grammar (comment-tolerant, whitespace-insensitive), produces `Vec<KidRule>` with preserved source spans. Implements `Distributor` for the rule set: takes `&EspWorld` + `&DeterministicChance`, produces patches into a sink. All KID filter evaluation lives here. No `skse-rs` dependency — pure compile-time logic.

**`mora-cli`** — the `mora` binary. Argument parsing (clap), Skyrim install auto-detection, orchestration, `miette`-based diagnostic rendering, writes `mora_patches.bin`.

**`mora-runtime`** — SKSE cdylib. On `kDataLoaded`: mmap patch file, deserialize, iterate, apply via `skse-rs`. Logs a summary.

**`mora-test-harness`** — SKSE cdylib that re-implements the existing `MoraTestHarness.dll` protocol in Rust. Opens TCP `127.0.0.1:9742`, responds to `status` / `dump <type>` / `lookup <fid>` / `quit` with JSONL. Bash-side hooks (`check.sh`) are unchanged.

**`xtask`** — dev-workflow orchestration. Not a runtime dependency of anything shipped. Commands include `capture-kid-goldens` (stages KID + a scenario on the self-hosted runner, dumps ground-truth keyword map), `stage-runner-image` (builds and uploads the runner-image refresh), `build-windows-dlls` (cross-compile convenience). Invoked via `cargo xtask <cmd>`.

### Cross-cutting

- **`thiserror`** for library error types per crate, **`anyhow`** in `mora-cli` for user-facing flow.
- **`tracing`** across all library crates; `mora-cli` installs a nice subscriber, `mora-runtime` pipes events to the SKSE log.
- **`cargo xtask`** for workflows heavier than a cargo alias (golden-data refresh, Windows cross-compile orchestration, runner-image update).

## The `Distributor` trait

The key extensibility point. Lives in `mora-core`.

```rust
pub trait Distributor {
    type Error: std::error::Error + Send + Sync + 'static;

    fn name(&self) -> &'static str;

    fn lower(
        &self,
        world: &EspWorld,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error>;
}
```

- `world: &EspWorld` — read-only indexed ESP data; the frontend never touches files directly.
- `chance: &DeterministicChance` — the KID-bit-identical RNG. Shared convention across frontends.
- `sink: &mut PatchSink` — append-only collector that dedupes identical `(opcode, target, payload)` patches and merges compatible ones.
- `DistributorStats` — counts (rules evaluated, candidates considered, patches emitted, chance-rolls rejected) for diagnostic output.

### Multi-frontend orchestration in `mora-cli`

```rust
let mut sink = PatchSink::new();
let chance = DeterministicChance::kid_compatible();

for distributor in configured_frontends() {
    distributor.lower(&world, &chance, &mut sink)?;
}

let patch_file = sink.finalize();
patch_file.write_to(output_path)?;
```

v1 registers one frontend (`KidDistributor`); SPID/SkyPatcher slot in later without changes to `mora-core` or `mora-cli`.

## KID lowering internals

### 1. INI discovery & parsing

Scan `Data/` for `*_KID.ini` files. Parse each into `Vec<KidRule>` with full source spans preserved for diagnostics.

Parser is hand-written recursive-descent. KID's syntax is simple enough that a parser generator (pest, lalrpop) is more overhead than it's worth. Parse errors attach `(file, line, column, snippet)` — users see clickable errors in their editors.

### 2. Filter AST

```rust
pub struct KidRule {
    pub source: SourceSpan,
    pub rule_type: RuleType,          // Keyword / ArmorKeyword / etc.
    pub keyword: KeywordRef,          // the keyword being distributed
    pub form_filters: FilterSet<FormRef>,
    pub string_filters: FilterSet<StringMatch>,
    pub keyword_filters: FilterSet<KeywordRef>,
    pub traits: FilterSet<TraitPredicate>,
    pub chance: u8,                   // 0..=100
}
```

`FilterSet<T>` encodes AND/OR/NOT explicitly rather than deferring boolean combination to evaluation time. This lets the evaluator short-circuit cleanly and enables future indexed-scan optimizations.

### 3. Candidate scanning

Rule's `rule_type` determines which record-type iterator to walk (`Weapon`, `Armor`, `Ingredient`, etc.). We walk the ESP world **once per record type**, not once per rule — all rules targeting weapons share a single pass. Inside each pass, each candidate is tested against every rule wanting that record type.

This is critical at scale: hundreds of rules × tens of thousands of candidates.

### 4. Deterministic chance roll

For each `(rule, candidate)` that passes all non-chance filters:

```rust
let seed = szudzik_pair(
    fnv1a_32(keyword.editor_id.as_bytes()),
    candidate.form_id.raw(),
);
let roll = DeterministicChance::draw_percent(seed);   // Xoshiro256** + MSVC uniform_real_distribution<float>(0, 100)
if roll <= rule.chance as f32 {
    sink.push(Patch::AddKeyword { target: candidate.form_id, keyword: keyword.form_id });
}
```

The draw reproduces KID's exact algorithm:

1. `raw = Xoshiro256StarStar::next_u64()` (state seeded from the 64-bit seed above via SplitMix64, matching `XoshiroCpp`).
2. `canonical_f32 = (raw as f64 / 2^64_f64) as f32` — MSVC's `generate_canonical<float, 24>` on a 64-bit engine reduces to double-precision divide narrowed to float.
3. If `canonical_f32 >= 1.0`, clamp to `0.0` (MSVC's out-of-range handling).
4. Return `100.0_f32 * canonical_f32`.

This is bit-identical to KID on Windows x64. Verified via unit-test vectors captured from a C++ test harness linked against XoshiroCpp + MSVC STL.

## Runtime behavior

**Load sequence** inside Skyrim during SKSE startup:

```
SKSE calls plugin_entry
  ├─► register for SKSE::MessagingInterface events
  └─► return success

on SKSEMessage::kDataLoaded
  ├─► locate Data/SKSE/Plugins/mora_patches.bin
  ├─► mmap it via skse-rs FileMapping helper
  ├─► deserialize PatchFile header
  ├─► verify magic + version
  ├─► verify load_order_hash == hash(live load order)
  │     └─► mismatch → log clear error, apply nothing, return
  ├─► for each Patch in patches:
  │     └─► Patch::AddKeyword { target, keyword }
  │         ├─► form         = TESDataHandler::LookupFormByID(target)
  │         ├─► keyword_form = TESDataHandler::LookupFormByID(keyword)
  │         ├─► if both resolve && form impls BGSKeywordForm:
  │         │     form->AddKeyword(keyword_form)
  │         └─► else: increment skipped counter, log at debug
  └─► log summary: "applied N patches, skipped M, took T ms"
```

### Key design choices

- **Load-order hash check is hard-fail.** Reordered plugins without recompile → FormIDs stale → could mutate wrong forms. Bail loudly; tell the user to re-run `mora compile`.
  The hash is a 64-bit digest (blake3 truncated) over the canonical sequence of `(plugin_name_lowercase, master_plugin_names_lowercase_in_order)` tuples in active load order. Stable against `plugins.txt` whitespace/comment differences; sensitive to any actual reorder, master change, or enable/disable.
- **No runtime FormID remapping.** Compiler resolves every FormID to its final runtime value for the user's load order.
- **Sequential patch application.** Not thread-safe under game load; sub-10ms anyway for the scale we expect.
- **Patch file is mmap'd, not slurped.** Lazy paging, no large malloc at a sensitive moment.

### Edge cases we handle explicitly

- Patch file missing → info-level log, skip. Not an error.
- Patch file truncated / bad magic / bad version → error + skip.
- Target form resolves but doesn't implement `BGSKeywordForm` → skip, debug-log. Shouldn't happen if compiler is correct; defensive.
- Plugin in hash isn't active → covered by the hash check.

### What the runtime does NOT do

- No FormID remapping.
- No KID INI parsing (ever).
- No filesystem scanning beyond the patch file.
- No save-game awareness.
- No in-game UI.

## CLI UX & diagnostics

### Commands

```
mora compile [--data-dir <path>] [--output <path>] [--dry-run] [--continue-on-error]
mora check   [--data-dir <path>]
mora info    [--data-dir <path>] [--plugin <name>]
```

### `mora compile`

The 95% case. Auto-detects Skyrim install if `--data-dir` is omitted:

- Windows: registry under `HKLM\SOFTWARE\Bethesda Softworks\Skyrim Special Edition`.
- Linux: walks Steam/Proton/Flatpak/XDG paths.

Auto-output to `<data-dir>/SKSE/Plugins/mora_patches.bin` when `--data-dir` was auto-detected, with a clear log line indicating installation. Explicit `--output` required when writing elsewhere.

Exit codes: `0` success, `1` user-fixable error (parse error, missing plugin), `2` internal error (panic or bug).

### `mora check`

Parse + validate INIs without emitting a patch. Useful for editor integration and CI.

### `mora info`

No args: load order summary, total rules parsed, per-frontend counts.
`--plugin Foo.esp`: which INIs reference it, which rules match records from it.

### Typical success output

```
$ mora compile
  Mora v0.1.0
  [OK] Detected Skyrim SE at /home/user/.steam/.../Skyrim Special Edition
  [OK] Loaded plugins.txt: 247 active plugins
  [OK] Parsed 34 KID INIs (1,892 rules)
  [OK] Scanned 1,247,301 records across 247 plugins (412ms)
  [OK] Lowered rules → 83,428 patches (156ms)
  [OK] Wrote mora_patches.bin (1.1 MB) to Data/SKSE/Plugins/
  Total: 623ms
```

### Error output (miette-rendered)

```
error: unknown filter trait on Weapon rule
  ╭─[Foo_KID.ini:14:8]
14 │   -INVALIDTRAIT, -PLAYABLE
   │    ────┬───────
   │        ╰── no such trait; did you mean -UNIQUE?
───╯
```

### Diagnostic severity

- **Error** — parse failures, unresolvable plugin references, malformed FormIDs. Blocks compilation unless `--continue-on-error`.
- **Warning** — rules matching zero records (probably a typo), `Chance = 0` rules, duplicate rules.
- **Info** — per-frontend stats.

### What the CLI does NOT do

- No `mora init` / scaffolding.
- No watch mode / daemon.
- No network / telemetry.
- No GUI.

## Testing strategy

Three layers. The existing Skyrim integration-test infrastructure (Unraid runner pool, `run-skyrim-test.sh` orchestrator, bash helpers, TCP harness protocol) is reused directly.

### Layer 1 — Unit tests

Standard Rust `#[cfg(test)]`. Hermetic, fast, no I/O, no game.

- **`mora-core`:** `DeterministicChance` reproduction vectors captured from KID's actual RNG path. A few hundred `(seed, expected_f32)` pairs covering `Chance = 0`, `= 100`, and float-rounding boundaries. Any drift → unit test fails.
  *Capture method:* a one-shot MSVC C++ program in `xtask/chance-vector-capture/` links against XoshiroCpp + MSVC STL, iterates a fixed set of seeds, dumps `(seed, uint32_bits_of_f32_result)` pairs to JSON. Run once on Windows (or on the self-hosted runner via Proton), commit the JSON under `crates/mora-core/tests/data/chance_vectors.json`. Re-captured only when KID updates its RNG stack (rare).
- **`mora-esp`:** hand-crafted synthetic ESP fixtures exercising header parsing, master resolution, record/subrecord iteration, compressed records, localized strings, edge cases.
- **`mora-kid`:** INI parser syntax fuzzing, filter-evaluation tests against in-memory `EspWorld` fixtures.
- **`mora-cli`:** argument parsing, install-detection logic (mocked FS).

Gates every PR via `cargo test --workspace`.

### Layer 2 — Golden tests vs. real KID

The compatibility proof. Without this we have a plausible-looking port; with it we have evidence.

**Corpus:**

- `tests/golden-data/kid-inis/` — curated real-world KID INIs from popular keyword mods + hand-crafted stress INIs covering every filter type.
- `tests/golden-data/scenarios/<name>/` — one scenario per record-type × filter-category combination.
- `tests/golden-data/expected/<scenario>.json` — committed ground-truth `(form_fid, applied_keywords)` maps.

**Golden capture** — a `cargo xtask capture-kid-goldens` task. Staging model: drop KID + the scenario's INIs into a mod-dir, run against the self-hosted runner's `/skyrim-base/` via `run-skyrim-test.sh`, use a one-shot harness command to dump the applied keyword map, commit the result. Automated; no dev-box-with-Windows requirement.

**Test loop** — for each scenario, run `mora compile`, extract patches from the output, diff against the committed expected JSON. Exact match or fail.

**Catches:** chance-algorithm drift, filter semantics errors, grammar misparses.

### Layer 3 — SKSE-in-the-loop integration tests

End-to-end: real runtime DLL inside real Skyrim via Proton, harness asserts on live game state.

**Reuses:**

- Self-hosted Unraid runner pool (3 parallel slots, expandable) — `10.20.77.32:/mnt/user/skyrim-runner/`.
- Baked image (updated for Rust toolchain — see M0 below).
- `run-skyrim-test.sh` orchestrator (language-agnostic; no changes required).
- `_lib/check_common.sh` bash helpers.
- Test-case layout: `tests/integration/<case>/{*_KID.ini, check.sh, README.md}`.

**Ported to Rust:**

- `mora-test-harness` cdylib replaces `MoraTestHarness.dll` — same TCP protocol on `127.0.0.1:9742`, same commands, same JSONL output.
- `mora-runtime` cdylib replaces `MoraRuntime.dll` — same filename so deployment scripts don't need edits.

Because the self-hosted runners make Layer 3 cheap, it runs on **every PR**, not just nightlies.

### What tests `skse-rs` itself

`skse-rs` ships with its own minimal test harness DLL + scenario. One Layer-3 smoke test that loads into Skyrim, looks up a known form, adds a known keyword, confirms via a second read. Lives in that crate.

## Migration & repo reset

**Pre-wipe:** tag the current state as `legacy-cpp-pre-pivot` so the old code is reachable by URL forever. No forward development on it, but existing users' binaries keep working and the history is preserved.

```
git tag legacy-cpp-pre-pivot
git push origin legacy-cpp-pre-pivot
```

**Wipe:** remove all existing source trees (`src/`, `include/`, `tests/`, `tools/`, `scripts/`, `data/`, `extern/`, `editors/`, `xmake.lua`, `compile_commands.json`, `build/`). Preserve `LICENSE`, `README.md` (will be rewritten), `docs/` (will be rewritten but old spec files remain historical), `.github/` (workflow file gets rewritten).

**Clean-slate commit:** the first Rust commit on `master` establishes the new workspace root.

**Runner image:** the Unraid-hosted baked image needs a manual update to swap C++ cross-compile tooling for Rust. Tracked as an explicit M0 task.

## Milestones

Estimates assume a single developer working steadily. Not commitments, calibration.

### M0 — Workspace skeleton & CI (1-2 days)

- Tag `legacy-cpp-pre-pivot`, wipe source trees, commit clean-slate workspace with empty crate stubs.
- CI: `cargo test --workspace`, `cargo clippy -- -D warnings`, `cargo fmt --check`, Windows cross-compile check via `cargo-xwin`.
- Update Unraid runner image: add rustup + stable + `cargo-xwin` + MSVC target; remove `xmake`, `gcc-13`, `xwin`-standalone, C++ headers.
- Seed KID compatibility matrix doc at `docs/src/kid-grammar.md` (filter types × record types, cells marked `❌ / 🟡 / ✅`), plus a derived summary block in the user guide. Grown per-PR as coverage lands.

### M1 — `skse-rs` minimal viable (1-2 weeks)

- SKSE plugin entry points + query/load + messaging interface.
- Address library loader (SE/AE target support).
- `TESDataHandler::LookupFormByID`, `BGSKeywordForm::AddKeyword`.
- SKSE log integration.
- Minimal harness DLL + one smoke test: load into Skyrim, look up a known form, add a known keyword, confirm.
- Publish to crates.io at 0.1.

### M2 — `mora-esp` + `mora-core` (1 week)

- mmap ESP reader, header + TES4 parsing, master resolution, plugins.txt loader.
- Record iteration for the full record type set (~20 types).
- Subrecord parsing for EDID, KWDA, record-type-specific trait subrecords (DNAM, FNAM, etc.).
- `mora-core`: `Patch` enum (AddKeyword only for now), `PatchFile` postcard format, `DeterministicChance` with MSVC/Xoshiro256** reproduction + captured-vector unit tests.

### M3 — `mora-kid` MVP: Weapon + Armor (1-2 weeks)

- INI parser for KID's full grammar (preserves spans for diagnostics).
- `Distributor` impl for Weapon + Armor, all filter categories (form/string/keyword/trait/chance).
- First end-to-end compile: KID INI → patch file.

### M4 — Golden-test harness & corpus (1 week)

- `cargo xtask capture-kid-goldens` automation (staged on self-hosted runners).
- Committed corpus of 5-10 real-world INI sets + hand-crafted stress cases.
- `cargo test` runs compile + diff against goldens.
- First true "100% compatible" proof on Weapon + Armor.

### M5 — `mora-runtime` + integration pipeline (1 week)

- SKSE cdylib, patch file mmap/deserialize/apply.
- `mora-test-harness` ports the TCP+JSONL protocol to Rust (same `127.0.0.1:9742` server, same commands, same JSONL dump format — existing `check.sh` scripts unchanged).
- Wire `tests/integration/<case>/` into the existing runner pool — first full end-to-end on every PR.

### M6 — Remaining KID record types (2-3 weeks)

- Per record type, one PR: `mora-esp` parsing additions + `mora-kid` filter evaluation + golden-test scenarios.
- Compatibility matrix moves from mostly-red to all-green.

### M7 — Distribution mode variants + edge-case traits (1 week)

- `DistributeByWeight`, `DistributeByValue`, rarer per-record-type traits.

### M8 — Polish, docs, 0.1 release (1 week)

- README rewrite around the KID-compatible scope.
- MkDocs site reset (remove language-reference / how-mora-works Datalog content; add user guide, filter reference, migration-from-KID note).
- Release binaries for Windows (`mora.exe` + `MoraRuntime.dll`) + Linux (`mora`), built in CI.

**Revised total: 7-10 weeks for full KID parity.** The existing Skyrim integration-test infrastructure shortens the original 9-12-week estimate materially.

## Risks

**Risk: `skse-rs` is bigger than it looks.**
Writing SKSE plugin infrastructure in Rust from scratch — address library, game struct layouts, vtable dispatch, messaging interface — is well-scoped but exacting. Layout mistakes cause silent memory corruption, not loud crashes.
**Mitigation:** M1's smoke test (look up a form, add a keyword, read it back) is the earliest end-to-end validation of the framework. If it works, the core is right. Every new type we add gets a similar smoke assertion.

**Risk: bit-identical KID chance drifts with MSVC STL changes.**
MSVC STL's `uniform_real_distribution<float>` is not spec-pinned; hypothetically a future MSVC update could change it, breaking goldens.
**Mitigation:** captured-vector unit tests pin the algorithm we implement. KID would drift the same way. We track Rust's algorithm separately from Microsoft's and only re-verify if a KID update is known to change chance behavior.

**Risk: KID INI grammar has undocumented edge cases.**
KID's published docs don't exhaustively specify every whitespace / comment / quoting behavior. Our parser may diverge subtly.
**Mitigation:** the golden corpus includes real-world INIs, not just hand-crafted ones. Parse-layer bugs surface as candidate-set differences in the golden diffs.

**Risk: `plugins.txt` semantics across MO2 / Vortex / standalone.**
Different mod managers write `plugins.txt` differently (in-place vs. synthesized vs. per-profile).
**Mitigation:** explicit MO2 / Vortex / vanilla test cases in the integration suite; documented guidance in the user guide.

**Risk: `mora-esp` correctness regressions as record types grow.**
Bespoke parser + 20 record types × varied subrecord layouts = surface area.
**Mitigation:** fuzzing (`cargo fuzz`) on the subrecord iterator starting in M2; golden-diff tests catch semantic drift.

## Decision log

Key tradeoffs made during brainstorming, for future reference.

- **Clean-slate wipe over keeping the C++ tree:** the C++ code is a different project, conceptually. Keeping it alongside would invite confusion and shared-plumbing leaks.
- **Rust everywhere (runtime included):** unified toolchain, one language. The cost is `skse-rs` from scratch — deliberate investment, produces a reusable artifact.
- **Drop-in KID INI compatibility over a new input format:** zero migration friction for the ecosystem; forces us to honor KID's semantic corners (which is the point of the pivot).
- **Per-frontend crate + `Distributor` trait over a single mega-crate:** compiler-enforced boundaries do the "segmented for SPID later" work; adds a few Cargo.tomls, trivial cost.
- **Bit-identical KID chance reproduction:** "100% compatible" is not credible if chance rolls diverge. Tractable (~20 lines of core arithmetic) but requires verification vectors.
- **Custom mmap ESP parser over `esplugin`:** our access pattern (scan every record of type X across hundreds of plugins, parallel) is different enough from LOOT's that extending `esplugin` costs similar effort without the perf ceiling.
- **Fully-resolved runtime FormIDs in the patch file over plugin-relative + runtime remap:** patch file is ephemeral, regenerated on any load-order change; runtime stays dumb.
- **Auto-install to detected Skyrim installs by default:** preserves current UX where the 95% case is `mora compile` and it just works.
- **Full KID parity as the v1 target (not MVP):** the "100% compatible" claim is load-bearing for user trust. Phased shipping (Weapon + Armor first, remaining record types per-PR) keeps the scope tractable.
- **Reuse existing Skyrim test infrastructure (Unraid runners, harness protocol, bash helpers):** reduces M4 / M5 substantially; the integration-test infrastructure is the non-obvious existing asset of the project.

## Open questions

- None after approval of Sections 1-7. If any surface during planning or implementation they get answered in the plan doc or in follow-up PRs.
