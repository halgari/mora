# M4 Follow-ups — Golden-test algorithmic fidelity

The M4 capture infrastructure (harness DLL, xtask capture pipeline, manifest, build.rs-generated test wiring) is complete and green. The ten `cargo test --test golden` scenarios are committed as `#[ignore]` tests because mora-kid / mora-esp have small algorithmic divergences from real KID that need focused investigation.

Each follow-up below is a self-contained tracking doc: symptom, shortest repro, leading hypothesis, relevant files, acceptance criterion.

## Index

- **[F0 — Captured goldens don't reflect KID's distribution](./f0-capture-doesnt-reflect-kid.md)** — **BLOCKER**. Despite KID running and loading our scenario INIs, the captured goldens show vanilla state (no keywords added). Discovered while investigating F1. F1/F2/F3 can't be properly verified until F0 is resolved.

- **[F1 — ANY-bucket filter semantics](./f1-any-bucket-filter.md)**
  `*Kw1,*Kw2` matches a different weapon set in mora-kid vs real KID. Worst offender; ~200 affected forms in `filter_keyword_any`. Highest-leverage follow-up — likely reveals mismatched bucketing logic that cascades to other scenarios. **Partial fix landed in F1 branch:** xtask staging moved scenario INIs from `Data/SKSE/Plugins/` to `Data/` (KID only scans `Data/`). KID now finds the INIs — but F0 remains.

- **[F2 — Armor template inheritance edge cases](./f2-armor-template-inheritance.md)**
  The test helper now walks `TNAM` chains and unions template keywords (landed in M4). A residual ~8 armors in the `0x8b6a2..0x8b6a9` range still have mora reporting extra keywords vs golden — the inheritance chain either overreaches or follows a TNAM Skyrim doesn't. Smallest-scope bug.

- **[F3 — Weapon trait predicate bisection](./f3-trait-predicates.md)**
  `traits_weapon_all`'s rule `OneHandSword,D(5 20),W(5.0 15.0),-E` matches ~50 more weapons than KID. One of the four predicates has subtly different semantics; bisection needed.

## Running the goldens locally

```bash
export MORA_SKYRIM_DATA=/path/to/Skyrim/Data  # or /skyrim-base/Data on the runner
cargo test --package mora-kid --test golden -- --ignored
```

On a run matching the manifest's ESP hashes, you'll see 10 FAILED with per-form diff output.

## Re-capturing goldens

Only needed when: KID version changes, Skyrim version changes on the runner, or a scenario INI changes.

```bash
cargo xtask capture-kid-goldens --all
```

Requires: self-hosted runner (or dev box with Skyrim + Proton), `third_party/kid/{KID.dll,KID.ini}` staged, `cargo-xwin` for the Windows build.
