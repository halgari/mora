# M4 — KID Golden-Test Harness & Corpus — Design Spec

**Date:** 2026-04-21
**Status:** Draft — approved in brainstorming, pending written review
**Parent:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md` (§ "M4 — Golden-test harness & corpus")

## Problem

M3 delivered a mora-kid distributor that produces plausible-looking KID output. Nothing yet proves that output actually matches real KID's behavior on the same input. Without a ground-truth oracle, every record type we add in M6 ships on hope, and any algorithm bug stays buried until someone notices an in-game discrepancy.

## Goal

Establish a cheap, reproducible, per-PR test that compares mora-kid's computed output against real KID's observable behavior on a fixed corpus of hand-crafted INIs. One golden-test failure should tell the reviewer exactly which form and which keyword diverged.

## Non-goals

- **No `mora-runtime.dll`.** Runtime work is M5.
- **No full TCP harness.** M4 ships a minimal capture-only DLL; the full TCP harness is M5.
- **No live end-to-end integration tests** (check.sh pattern against running Skyrim). Also M5.
- **No goldens beyond WEAP + ARMO.** M6 adds scenarios per record type as `mora-esp`+`mora-kid` coverage lands.
- **No real-world third-party KID INIs.** Licensing + mod-ESP dependencies rule this out at M4. Hand-crafted only.
- **No per-rule attribution / log scraping.** Post-state diff is sufficient for "does mora-kid compute what KID computes?"

## Architecture

Two fully decoupled loops:

```
  ┌──────────── CAPTURE (explicit, rare, Skyrim-dependent) ─────────────┐
  │                                                                     │
  │  cargo xtask capture-kid-goldens [--scenario N | --all]             │
  │                                                                     │
  │   ┌───────────────┐   ┌────────────────────┐   ┌─────────────────┐  │
  │   │ stage mod-dir │ → │ run-skyrim-test.sh │ → │ pull dump files │  │
  │   │  KID.dll +    │   │  (proton + xvfb)   │   │  weapons.jsonl  │  │
  │   │  scenario INI │   │                    │   │  armors.jsonl   │  │
  │   │  harness DLL  │   │                    │   │                 │  │
  │   └───────────────┘   └────────────────────┘   └─────────────────┘  │
  │                                                        │            │
  │                                                        ▼            │
  │                                              commit to repo:        │
  │                                   tests/golden-data/expected/<N>/   │
  └─────────────────────────────────────────────────────────────────────┘

  ┌──────────── TEST (every PR, Skyrim-data-dependent only) ────────────┐
  │                                                                     │
  │  cargo test --package mora-kid --test golden                        │
  │                                                                     │
  │   ┌─────────────┐   ┌───────────────────┐   ┌──────────────────┐    │
  │   │ read corpus │   │ mora-esp parse    │   │ mora-kid::lower  │    │
  │   │ INI + ESPs  │ → │ vanilla keywords  │ → │ compute patches  │    │
  │   └─────────────┘   └───────────────────┘   └──────────────────┘    │
  │                                                        │            │
  │                                                        ▼            │
  │                                         post_state = vanilla ∪      │
  │                                                     mora_patches    │
  │                                                        │            │
  │                                                        ▼            │
  │                                   diff against expected/<N>/*.jsonl │
  └─────────────────────────────────────────────────────────────────────┘
```

**Why decouple.** Capture is expensive (Skyrim boot ~60s × 10 scenarios ≈ 10 min) and flaky (Proton, Xvfb, game init). Tests are cheap (parse ESPs + distributor + JSON diff, <1 sec per scenario). Forcing capture every PR would burn 10 minutes of CI to re-derive a value that already exists as committed bytes. The committed JSONL *is* the contract.

## Corpus

### Layout

```
tests/golden-data/
  kid-inis/
    <scenario>/
      <scenario>_KID.ini        # the rule set under test (one or more files)
      README.md                 # one-paragraph description
  expected/
    <scenario>/
      weapons.jsonl             # captured post-state dump
      armors.jsonl
      manifest.json             # hashes + versions (see below)
```

### Scenarios

Ten hand-crafted scenarios, each targeting a specific algorithm surface. Each INI uses only vanilla + DLC + CC forms so it runs against the `/skyrim-base/` load order without additional mod dependencies.

| Scenario | Exercises |
|---|---|
| `filter_form_positive` | `+FormID` ALL-bucket, single rule adds a keyword to every Iron weapon |
| `filter_form_negative` | `-FormID` NOT-bucket, subtracts a specific form from the positive match set |
| `filter_edid_substring` | `+EditorID` ALL-bucket, substring match (e.g., contains `Iron`) |
| `filter_keyword_any` | `*Keyword` ANY-bucket, multiple keyword candidates |
| `filter_keyword_not` | `-Keyword` NOT-bucket, excludes forms that already carry a given keyword |
| `traits_weapon_all` | every weapon trait predicate: animation type, speed, reach, value, weight, damage, enchanted, template |
| `traits_armor_all` | every armor trait predicate: armor type, body slot, armor rating, value, weight, enchanted, template |
| `chance_boundary` | three rules with `Chance=0`, `Chance=50`, `Chance=100`; proves the deterministic RNG stack matches |
| `exclusive_group` | two overlapping rules in an `[Exclusive Group]`; proves first-wins ordering matches KID's |
| `esl_light_forms` | rule targeting forms in a 0xFE-prefixed ESL plugin (CC contains ESLs); proves plugin-index resolution |

If a scenario proves noisy or thin during implementation, drop or split — the scenario directory is the atomic unit. Scenario count isn't a target.

### Data-dump shape

Full post-state: every WEAP and ARMO form and its complete keyword list at `kDataLoaded`-time, one form per line:

```jsonl
{"form":"0x00012345","kws":["0x0001AABB","0x0001BBCC"]}
{"form":"0x00012346","kws":["0x0001AABB"]}
```

`form` and `kws` are lowercase-hex `0x` FormIDs, resolved to Skyrim's in-memory 32-bit IDs (regular plugin index in the high byte; ESL plugin index is 0xFE-prefixed with a 12-bit sub-index, same representation the game uses). Keyword lists are sorted ascending for stable diffs. Empty keyword lists are omitted entirely (no line for that form), and the test-time post-state map omits empty entries the same way so the two sides compare like-for-like.

### Manifest

`manifest.json` alongside the JSONL files:

```json
{
  "captured_at": "2026-04-28T14:02:11Z",
  "kid_version": "5.6.0",
  "skyrim_version": "1.6.1170",
  "esp_hashes": {
    "Skyrim.esm": "sha256-...",
    "Update.esm": "sha256-...",
    "Dawnguard.esm": "sha256-...",
    "...": "..."
  }
}
```

On test startup we re-hash every ESP under `MORA_SKYRIM_DATA` and compare. Mismatch → **skip not fail** with a clear diagnostic. A Skyrim patch must never red CI on an unrelated PR; the fix is a manual re-capture.

## Components

### `crates/mora-golden-harness/` — minimal capture-only SKSE DLL

Purpose: dump every WEAP + ARMO form's keyword list once on `kDataLoaded`, then signal completion. No TCP, no commands, no long-lived listener — just write files and exit-signal.

Interface with skse-rs:

- `SKSEPlugin_Version`, `SKSEPlugin_Query`, `SKSEPlugin_Load` (standard).
- `kDataLoaded` handler: enumerate `TESDataHandler::formArrays[TESObjectWEAP]` and `[TESObjectARMO]`, walk `BGSKeywordForm::keywords` per form, write sorted JSONL to `Data/MoraCache/dumps/{weapons,armors}.jsonl`, write a sentinel file `Data/MoraCache/dumps/.done`.

Size target: ~200 LoC. Narrow enough that one reader can audit it in ten minutes.

When M5 builds the full `mora-test-harness` (TCP + multiple dump types + lookup + quit), it subsumes this crate. At that point this crate becomes either (a) a thin wrapper around the full harness's dump-only entry point, or (b) deleted and the capture xtask drives the full harness. Decision is M5's; record as an M5 plan task ("consolidate with `mora-golden-harness`").

### `xtask/capture-kid-goldens/` — orchestration binary

Runs on the self-hosted runner or any dev box with Skyrim installed.

1. `cargo xwin build --target x86_64-pc-windows-msvc -p mora-golden-harness --release`.
2. For each selected scenario (serial):
   1. Build a clean staging mod-dir at `/tmp/mora-capture/<scenario>/Data/SKSE/Plugins/` containing:
      - `KID.dll` (pinned version, baked into runner image — see below)
      - `KID.ini` (KID's default global settings)
      - the scenario's `*_KID.ini`
      - `MoraGoldenHarness.dll` (the DLL built in step 1)
   2. Invoke `run-skyrim-test.sh` with a one-line `check.sh` hook that polls for the harness's sentinel file, then exits 0.
   3. Copy `Data/MoraCache/dumps/{weapons,armors}.jsonl` from the run's cwd out to `tests/golden-data/expected/<scenario>/`.
   4. Hash every `.esp/.esm/.esl` in the Skyrim data dir; read KID.dll's file version; write `manifest.json`.
3. Print a summary: `captured 10/10 scenarios, total 4.2 MB`.

Flags: `--scenario NAME` runs one, `--all` runs every scenario directory under `tests/golden-data/kid-inis/`.

### `crates/mora-kid/tests/golden.rs` — per-PR test

`build.rs` discovers `tests/golden-data/expected/*/` directories and emits one `#[test]` per scenario:

```rust
#[test]
fn golden_filter_form_positive() {
    run_golden_scenario("filter_form_positive");
}
```

Helper `run_golden_scenario(name)`:

1. Check `MORA_SKYRIM_DATA` — if unset, skip with a clear diagnostic.
2. Re-hash every ESP under that path, diff against `manifest.json` — if mismatch, skip with a clear diagnostic.
3. Load the scenario's `*_KID.ini` via `mora-kid::parse`.
4. Load the ESPs via `mora-esp::EspWorld::open`.
5. Run `KidDistributor::lower(&world, &chance, &mut sink)`, collect patches.
6. Compute `post_state[form] = keywords_on_record_as_parsed_from_ESP[form] ∪ patches_for[form]`, then sort each keyword list. "Keywords on record as parsed from ESP" means the `KWDA` subrecord contents at rest — the same baseline Skyrim loads before any SKSE plugin runs.
7. Load `expected/<name>/weapons.jsonl` + `armors.jsonl` into a `HashMap<FormId, Vec<FormId>>`.
8. Assert per-form equality; on mismatch, emit:
   ```
   scenario filter_form_positive: form 0x00012345 diverges
     expected: [0x0001AABB, 0x0001BBCC]
     actual:   [0x0001AABB]
     missing:  [0x0001BBCC]
   ```

Empty keyword lists are treated consistently on both sides: "not in JSONL" == "empty vec" == "form not in mora post_state keyed map."

## CI integration

Add one step to the existing `skyrim-integration` self-hosted job in `.github/workflows/ci.yml`:

```yaml
- name: KID golden tests
  env:
    MORA_SKYRIM_DATA: /skyrim-base/Data
  run: cargo test --package mora-kid --test golden
```

Runs on every PR, alongside the existing live-in-game tests. On the fast `test` job (Ubuntu, no Skyrim) the golden tests skip — that's by design.

## Runner image change

One-time: bake KID into the runner image.

- Add `/skyrim-baseline/optional-plugins/KID/KID.dll` — pinned version (e.g., 5.6.0).
- Add `/skyrim-baseline/optional-plugins/KID/KID.ini` — KID's default global settings.
- Document in `docs/src/runner-image-refresh.md`, including the pinned version.

After the image rebuild it's baked in and no per-capture install is needed.

## Error handling

Every failure mode maps to a clear user action:

| Failure | Mode | Diagnostic | Fix |
|---|---|---|---|
| `MORA_SKYRIM_DATA` unset | skip | `skipping golden tests: set MORA_SKYRIM_DATA to a Skyrim Data dir to run` | set env var or ignore |
| ESP hash mismatch | skip | `skipping scenario X: Skyrim.esm hash differs from goldens captured 2026-04-28` | re-capture |
| Scenario INI missing from disk | panic in build.rs | `scenario dir has expected/ but no kid-inis/` | fix corpus layout |
| post_state mismatch | test fail | per-form expected/actual/missing diff (see above) | fix mora-kid or re-capture |
| `cargo xtask capture` harness fails to dump | return non-zero, no commit | surfaced in `run-skyrim-test.sh` log | debug harness or Skyrim install |
| `cargo xtask capture` Skyrim crashes on boot | `run-skyrim-test.sh` times out | stashed logs in `$LOG_DIR` | debug runner environment |

## First-capture bootstrap order

No chicken-and-egg:

1. Build `mora-golden-harness` + `xtask capture-kid-goldens` (no goldens yet, nothing to diff).
2. Commit scenario INIs under `tests/golden-data/kid-inis/`.
3. Run the xtask once on the runner to generate the first `expected/` set.
4. Commit the captured `expected/` + `manifest.json` files.
5. Wire `mora-kid/tests/golden.rs` + `build.rs`.
6. CI step lands green.

Each step is a commit; the intermediate states all build.

## Invalidation & refresh

Goldens go stale when any of the following change:

- A scenario INI changes — re-run `cargo xtask capture-kid-goldens --scenario <name>` for that scenario only.
- KID's version changes — re-run `--all`, commit in a single "refresh goldens for KID X.Y.Z" PR.
- The runner's Skyrim version changes — re-run `--all`, single refresh PR. Tests on open PRs skip (not fail) until that refresh lands.
- mora's algorithm changes in a way that intentionally diverges from KID (shouldn't happen in M4–M8) — stop; this is an escalation.

Refresh PRs are the only time goldens change. Tag them `golden-refresh` for reviewer speed.

## Dependencies

- `skse-rs` (M1) — form array enumeration, `BGSKeywordForm::keywords` access.
- `mora-esp` (M2) — ESP hashing, vanilla keyword reconstruction.
- `mora-kid` (M3) — distributor to diff against.
- Existing Skyrim integration infra — `run-skyrim-test.sh`, runner pool, image build process.

## Success criteria

- All 10 scenarios capture cleanly on the self-hosted runner via `cargo xtask capture-kid-goldens --all`.
- `cargo test --package mora-kid --test golden` passes on the `skyrim-integration` CI job.
- A deliberate bug introduced into `mora-kid::Distributor::lower` (e.g., flip a filter condition) makes at least one scenario fail with a diff that points at the right form.
- Total committed size of `tests/golden-data/` under 10 MB uncompressed.
- Capture takes under 15 minutes serial for `--all`.

## Out-of-scope (future work)

- **Real-world INI corpus (B/C from brainstorming).** Deferred — add scenario-by-scenario in M6 as specific third-party INIs prove worth replicating (subject to licensing/permission).
- **Purpose-built fixture ESPs.** Deferred — vanilla+CC coverage is sufficient for M4's combinatorial completeness.
- **Parallel capture across runners.** Sequential is 10 min for --all; parallel saves ~7 min, not worth the orchestration cost at this scale.
- **Capture-on-every-PR.** Intentionally rejected; committed JSONL is the contract.
- **Per-rule emit attribution / KID log scraping.** Post-state diff is sufficient to catch algorithm divergence. Per-rule attribution belongs to a future diagnostics feature, not M4.
