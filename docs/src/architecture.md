# Mora Architecture — M0 Snapshot

A light-weight orientation doc. For the full design, see
[`docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`](../superpowers/specs/2026-04-20-rust-kid-pivot-design.md).

## Workspace layout

```
mora/
├── Cargo.toml                 workspace root
├── crates/
│   ├── skse-rs/               Rust SKSE plugin framework (M1)
│   ├── mora-core/             patch format, Distributor trait, chance RNG (M2)
│   ├── mora-esp/              mmap ESP/ESL/ESM reader + plugins.txt (M2)
│   ├── mora-kid/              KID INI parser + lowering (M3, M6, M7)
│   ├── mora-cli/              `mora` binary (M3 first, M5/M8 refinements)
│   ├── mora-runtime/          MoraRuntime.dll SKSE plugin (M5)
│   ├── mora-test-harness/     MoraTestHarness.dll (M5, ports the C++ harness)
│   └── xtask/                 dev-workflow commands (M4+)
├── tests/integration/         bash-driven Skyrim-in-Proton cases (legacy layout, rewired in M5)
├── docs/
│   ├── src/                   MkDocs source
│   ├── plans/                 legacy C++ plans (archived)
│   └── superpowers/           the pivot spec + per-milestone plans
└── .github/workflows/ci.yml   cargo-native CI, self-hosted Skyrim gate
```

## Dependency graph

```
mora-cli ─► mora-kid ─► mora-core ◄─ mora-esp
              │                       ▲
              └───────────────────────┘

mora-runtime      ─► mora-core, skse-rs
mora-test-harness ─► mora-core, skse-rs
xtask             ─► mora-cli
```

No cycles. `mora-core` is the shared trunk; `skse-rs` is independent
and will be published to crates.io separately.

## Data flow (once M1-M5 land)

```
plugins.txt + Data/*.esp  →  [mora-esp]   →  EspWorld (mmapped, indexed)
*_KID.ini                 →  [mora-kid]   →  Vec<KidRule>
  EspWorld + KidRules     →  lower()      →  Vec<Patch>
  Vec<Patch>              →  postcard     →  mora_patches.bin

In-game (kDataLoaded):
  mora_patches.bin  →  [mora-runtime]  →  AddKeyword calls via skse-rs
```

## Current state

Only M0 scaffolding exists:
- All crates are empty stubs that compile but do nothing.
- CI enforces fmt/clippy/test/cross-compile.
- Documentation skeleton is in place.

Real functionality arrives in M1 (skse-rs) through M8 (0.1 release).
