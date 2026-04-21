# Mora

**Mora is pivoting.** The project is being rebuilt in Rust as a
precomputing, drop-in-compatible replacement for powerof3's
[Keyword Item Distributor (KID)](https://github.com/powerof3/Keyword-Item-Distributor).

- The prior C++ Datalog-language implementation is archived at the
  `legacy-cpp-pre-pivot` tag. It still builds, and its documentation
  remains in `docs/`, but no forward development is happening on it.
- The Rust rewrite lives on `master` going forward. Current state:
  workspace scaffolding only. Actual functionality arrives in
  subsequent milestones.

**Goal:** point Mora at a Skyrim install with KID INIs in `Data/`,
get a precomputed `mora_patches.bin` that (together with
`MoraRuntime.dll`) applies the exact same keyword distribution KID
would, without KID itself installed, without a plugin slot, with
chance rolls and filter evaluation all resolved at compile time.

See [`docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`](docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md)
for the full design.

## Status

| Milestone | Scope | Status |
|-----------|-------|--------|
| M0 | Workspace + CI + docs scaffolding | in progress |
| M1 | `skse-rs` minimal SKSE framework | not started |
| M2 | `mora-esp` + `mora-core` | not started |
| M3 | `mora-kid` MVP (Weapon + Armor) | not started |
| M4 | Golden-test harness + corpus | not started |
| M5 | `mora-runtime` + integration pipeline | not started |
| M6 | Remaining KID record types | not started |
| M7 | Distribution mode variants + edge-case traits | not started |
| M8 | Polish, docs rewrite, 0.1 release | not started |

## License

[MPL 2.0](LICENSE)
