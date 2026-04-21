# Rust + KID Pivot — Plan 1: Foundation (M0)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wipe the existing C++ Datalog project, land a clean Rust cargo workspace with all eight crates stubbed, set up CI that enforces `cargo test`/`clippy`/`fmt` and a Windows cross-compile check, and scaffold the documentation that subsequent milestones will fill in.

**Architecture:** Preserve existing history under the `legacy-cpp-pre-pivot` tag. On a new branch, delete the C++ source trees and add a cargo workspace with empty crate stubs (all of which compile but do nothing). Replace the xmake/clang-tidy CI with cargo-native CI using GitHub-hosted runners for the fast path; leave the self-hosted Skyrim integration matrix disabled until M5 wires the Rust harness in.

**Tech Stack:** Rust 1.85+ (stable), cargo workspace, `cargo-xwin` for MSVC cross-compile from Linux, GitHub Actions. No code in this plan — this is entirely workspace scaffolding, CI rewrites, and docs scaffolding.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope note:** This plan implements spec milestone **M0 only**. Milestones M1 (skse-rs) through M8 (polish) each get their own plan, written in sequence after the prior milestone lands so each plan benefits from what we learned.

---

## File Structure

**Deleted:**
- `src/` — entire C++ source tree
- `include/` — C++ public headers
- `tests/` — C++ gtest suite (the `tests/integration/` bash scripts under it stay, moved aside temporarily then restored)
- `tools/` — Python metaprogramming helpers
- `scripts/` — xmake/cross-compile scripts (some bash helpers preserved; see Task 2)
- `data/` — YAML relation specs
- `extern/` — CommonLibSSE-NG submodule + other vendored C++
- `editors/` — VS Code extension source
- `xmake.lua`
- `compile_commands.json`
- `build/` (generated artifact, should already be gitignored)
- `.github/workflows/clang-tidy.yml`

**Preserved unchanged:**
- `LICENSE`
- `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md` (this pivot's spec)
- `docs/superpowers/plans/2026-04-20-rust-kid-pivot-plan-1-foundation.md` (this plan)
- `docs/src/integration-testing.md` (still describes the reusable harness infrastructure)

**Preserved but will be rewritten in later milestones (M8):**
- Other `docs/src/*.md` (language-reference, how-mora-works, etc.) — stay as historical artifacts until M8 does the MkDocs overhaul.
- `docs/mkdocs.yml`

**Preserved and moved:**
- `tests/integration/` (the bash test case layout) → staged aside during wipe, restored inside the new workspace at the same path. The actual `*.mora` files inside cases will be replaced with `*_KID.ini` in M5.

**Created:**
- `README.md` — rewritten as pivot notice
- `Cargo.toml` — workspace root manifest
- `rust-toolchain.toml` — pin Rust version
- `rustfmt.toml` — formatter config
- `clippy.toml` — lint config
- `.cargo/config.toml` — cross-compile target config
- `.gitignore` — rewritten for Rust
- `crates/skse-rs/Cargo.toml` + `src/lib.rs` — crate stub
- `crates/mora-core/Cargo.toml` + `src/lib.rs` — crate stub
- `crates/mora-esp/Cargo.toml` + `src/lib.rs` — crate stub
- `crates/mora-kid/Cargo.toml` + `src/lib.rs` — crate stub
- `crates/mora-cli/Cargo.toml` + `src/main.rs` — binary stub
- `crates/mora-runtime/Cargo.toml` + `src/lib.rs` — cdylib stub
- `crates/mora-test-harness/Cargo.toml` + `src/lib.rs` — cdylib stub
- `crates/xtask/Cargo.toml` + `src/main.rs` — binary stub
- `.github/workflows/ci.yml` — rewritten for cargo
- `docs/src/kid-grammar.md` — compatibility matrix skeleton
- `docs/src/architecture.md` — M0 architecture snapshot

---

## Phase A — Git Preparation (Tasks 1-3)

### Task 1: Tag legacy master, push tag, create pivot branch

**Files:** none modified in-repo; this is pure git state management.

- [ ] **Step 1: Confirm we're up to date with origin/master**

Run:
```bash
cd /home/tbaldrid/oss/mora
git fetch origin
git status
```

Expected: current HEAD is at or equivalent to `origin/master` (or a detached HEAD at the same commit). If local work is pending, stash or commit it before continuing. There should be no uncommitted changes before wiping.

- [ ] **Step 2: Create and push the legacy tag**

Run:
```bash
git tag -a legacy-cpp-pre-pivot origin/master -m "Final state of the C++ Datalog implementation before the Rust + KID pivot"
git push origin legacy-cpp-pre-pivot
```

Expected: tag exists locally and remotely. The tag pins the last C++ commit so the old code remains reachable indefinitely.

- [ ] **Step 3: Create the pivot working branch from master**

Run:
```bash
git checkout -B rust-kid-pivot origin/master
```

Expected: now on `rust-kid-pivot`, pointing at the same commit as `origin/master`. All subsequent work in this plan commits here.

- [ ] **Step 4: Verify**

Run:
```bash
git branch --show-current
git describe --tags
```

Expected output:
```
rust-kid-pivot
legacy-cpp-pre-pivot
```

No commit yet; this task is git-state only.

---

### Task 2: Wipe C++ source trees, stage integration-test harness cases aside

**Files:** mass deletion + a temporary move of `tests/integration/`.

- [ ] **Step 1: Preserve the bash-based integration test layout**

The `tests/integration/` directory contains bash `check.sh` scripts and `_lib/check_common.sh` helpers that are reusable as-is (spec Section 6). Move them aside before the wipe so they survive, then restore inside the Rust workspace.

Run:
```bash
mkdir -p /tmp/mora-preserve
mv tests/integration /tmp/mora-preserve/integration
```

Expected: `/tmp/mora-preserve/integration/` now contains `_lib/`, case directories, and `README.md`; `tests/integration/` no longer exists in the repo.

- [ ] **Step 2: Delete the C++ source trees**

Run:
```bash
rm -rf src include tests tools scripts data extern editors build
rm -f xmake.lua compile_commands.json
```

Expected: those paths no longer exist. `docs/` still exists. `LICENSE` and `README.md` still exist.

- [ ] **Step 3: Delete the clang-tidy workflow**

The clang-tidy workflow is C++-specific and has no Rust analog (clippy covers that role).

Run:
```bash
rm -f .github/workflows/clang-tidy.yml
```

Expected: `.github/workflows/` still exists, but `clang-tidy.yml` is gone.

- [ ] **Step 4: Rewrite .gitignore for Rust**

Run:
```bash
cat > .gitignore <<'EOF'
# Rust
/target/
**/*.rs.bk
Cargo.lock.bak

# IDE
.vscode/
.idea/
*.swp

# OS
.DS_Store

# Secrets / local state
.env
.env.local

# Build artifacts (legacy; retained to prevent accidental re-commit during transition)
/build/
/compile_commands.json
EOF
```

Expected: `.gitignore` now reflects a Rust project layout.

- [ ] **Step 5: Restore the integration test case layout into its new home**

`tests/integration/` lives at the workspace root in the new layout (same place as before — cargo is fine with a top-level `tests/` directory even without a crate claiming it, and the bash scripts are not part of any cargo test).

Run:
```bash
mkdir -p tests
mv /tmp/mora-preserve/integration tests/integration
rmdir /tmp/mora-preserve
```

Expected: `tests/integration/` is restored at the repo root, content-identical to before the wipe.

- [ ] **Step 6: Verify the working tree matches the intended wiped state**

Run:
```bash
ls -la
git status --short | head -40
```

Expected top-level entries: `LICENSE`, `README.md`, `docs/`, `tests/` (only `tests/integration/`), `.github/` (only `ci.yml` and `docs.yml` workflows), `.gitignore`. Everything else (`src/`, `include/`, `extern/`, etc.) is gone. `git status` shows a large block of `D` entries for the deleted files.

- [ ] **Step 7: Commit the wipe**

Run:
```bash
git add -A
git commit -m "chore: wipe C++ Datalog tree for Rust + KID pivot

Preserves tests/integration/ (bash harness layout is language-agnostic,
reused in M5). C++ source trees, xmake, compile_commands, extern/
submodules, clang-tidy workflow, and the VS Code extension all removed.
The prior state is reachable at tag legacy-cpp-pre-pivot."
```

Expected: commit lands on `rust-kid-pivot`. `git log --stat -1` shows a large block of deletions and nothing added.

---

### Task 3: Rewrite README.md as a pivot notice

**Files:**
- Modify: `README.md` (complete rewrite)

- [ ] **Step 1: Overwrite README.md**

Run:
```bash
cat > README.md <<'EOF'
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
EOF
```

- [ ] **Step 2: Commit**

Run:
```bash
git add README.md
git commit -m "docs: rewrite README as pivot notice with milestone status table"
```

Expected: commit lands. `git log --oneline -2` shows two fresh commits on `rust-kid-pivot`.

---

## Phase B — Cargo Workspace Scaffolding (Tasks 4-6)

### Task 4: Root workspace manifest + toolchain pin + formatter/lint config

**Files:**
- Create: `Cargo.toml`
- Create: `rust-toolchain.toml`
- Create: `rustfmt.toml`
- Create: `clippy.toml`
- Create: `.cargo/config.toml`

- [ ] **Step 1: Create the workspace root `Cargo.toml`**

Run:
```bash
cat > Cargo.toml <<'EOF'
[workspace]
resolver = "2"
members = [
    "crates/skse-rs",
    "crates/mora-core",
    "crates/mora-esp",
    "crates/mora-kid",
    "crates/mora-cli",
    "crates/mora-runtime",
    "crates/mora-test-harness",
    "crates/xtask",
]

[workspace.package]
version = "0.1.0"
edition = "2024"
license = "MPL-2.0"
repository = "https://github.com/halgari/mora"
rust-version = "1.85"

[workspace.dependencies]
# Shared utility crates. Versions pinned here; member crates refer via `workspace = true`.
thiserror = "2"
anyhow = "1"
tracing = "0.1"
tracing-subscriber = { version = "0.3", features = ["env-filter", "fmt"] }
serde = { version = "1", features = ["derive"] }
postcard = { version = "1", features = ["use-std"] }
clap = { version = "4", features = ["derive"] }
miette = { version = "7", features = ["fancy"] }
memmap2 = "0.9"
blake3 = "1"
rayon = "1"

[profile.release]
lto = "fat"
codegen-units = 1
strip = "symbols"
EOF
```

Rationale for each choice: Edition 2024 matches the spec's 1.85 minimum; resolver 2 is required for workspaces; dep versions are current stable majors as of 2026-04; `lto = "fat"` and single codegen unit maximize runtime DLL perf at the cost of build time.

- [ ] **Step 2: Pin the Rust toolchain**

Run:
```bash
cat > rust-toolchain.toml <<'EOF'
[toolchain]
channel = "1.85"
components = ["rustfmt", "clippy"]
targets = ["x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc"]
EOF
```

Rationale: pinning the channel means CI and dev boxes agree on the compiler; the two targets cover Linux-native for the CLI/tools and Windows-MSVC for the SKSE DLLs.

- [ ] **Step 3: Create `rustfmt.toml`**

Run:
```bash
cat > rustfmt.toml <<'EOF'
edition = "2024"
max_width = 100
use_field_init_shorthand = true
use_try_shorthand = true
EOF
```

- [ ] **Step 4: Create `clippy.toml`**

Run:
```bash
cat > clippy.toml <<'EOF'
# Keep clippy strict but not pedantic-by-default.
msrv = "1.85"
EOF
```

- [ ] **Step 5: Create `.cargo/config.toml` for cross-compile**

Run:
```bash
mkdir -p .cargo
cat > .cargo/config.toml <<'EOF'
# Cross-compile configuration.
#
# Linux-hosted Windows builds use cargo-xwin, which provides the MSVC
# toolchain + Windows SDK automatically. Run:
#
#   cargo xwin build --release --target x86_64-pc-windows-msvc
#
# No per-target linker overrides are needed — cargo-xwin handles that.

[alias]
xtask = "run --package xtask --"
EOF
```

- [ ] **Step 6: Commit**

Run:
```bash
git add Cargo.toml rust-toolchain.toml rustfmt.toml clippy.toml .cargo/config.toml
git commit -m "build: cargo workspace root manifest + toolchain + rustfmt/clippy config

- Rust 1.85 (edition 2024), targets Linux + Windows-MSVC.
- Workspace pins thiserror, anyhow, tracing, serde, postcard, clap,
  miette, memmap2, blake3, rayon.
- cargo xwin is the cross-compile path from Linux to Windows; no linker
  overrides needed in .cargo/config."
```

---

### Task 5: Create the eight crate stubs

**Files:**
- Create: `crates/skse-rs/Cargo.toml`, `crates/skse-rs/src/lib.rs`
- Create: `crates/mora-core/Cargo.toml`, `crates/mora-core/src/lib.rs`
- Create: `crates/mora-esp/Cargo.toml`, `crates/mora-esp/src/lib.rs`
- Create: `crates/mora-kid/Cargo.toml`, `crates/mora-kid/src/lib.rs`
- Create: `crates/mora-cli/Cargo.toml`, `crates/mora-cli/src/main.rs`
- Create: `crates/mora-runtime/Cargo.toml`, `crates/mora-runtime/src/lib.rs`
- Create: `crates/mora-test-harness/Cargo.toml`, `crates/mora-test-harness/src/lib.rs`
- Create: `crates/xtask/Cargo.toml`, `crates/xtask/src/main.rs`

Each crate has a Cargo.toml declaring its name/type/dependencies, and a trivial `lib.rs` / `main.rs` that exists so `cargo check` passes. Real content lands in later milestones.

- [ ] **Step 1: Create the `skse-rs` stub**

Run:
```bash
mkdir -p crates/skse-rs/src
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
EOF
cat > crates/skse-rs/src/lib.rs <<'EOF'
//! `skse-rs` — Rust-native SKSE plugin framework.
//!
//! This crate will grow in milestone M1 (see
//! `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`,
//! section "skse-rs"). M0 only establishes the crate stub so the
//! workspace compiles.
EOF
cat > crates/skse-rs/README.md <<'EOF'
# skse-rs

Clean-room Rust SKSE plugin framework for Skyrim SE/AE. Not a binding
to or wrapper around the C++ CommonLibSSE-NG library.

**Status: stub.** Real functionality lands in Mora's M1 milestone.
See the Mora pivot spec for the scope plan.
EOF
```

- [ ] **Step 2: Create the `mora-core` stub**

Run:
```bash
mkdir -p crates/mora-core/src
cat > crates/mora-core/Cargo.toml <<'EOF'
[package]
name = "mora-core"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "Shared types, patch format, chance RNG, and Distributor trait for Mora."
publish = false

[dependencies]
thiserror.workspace = true
serde.workspace = true
postcard.workspace = true
tracing.workspace = true
EOF
cat > crates/mora-core/src/lib.rs <<'EOF'
//! Core shared types for Mora: patch format, FormID newtypes, chance
//! RNG, and the `Distributor` trait.
//!
//! Populated in milestone M2.
EOF
```

- [ ] **Step 3: Create the `mora-esp` stub**

Run:
```bash
mkdir -p crates/mora-esp/src
cat > crates/mora-esp/Cargo.toml <<'EOF'
[package]
name = "mora-esp"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "Memory-mapped ESP/ESL/ESM parser and load-order resolver for Mora."
publish = false

[dependencies]
mora-core = { path = "../mora-core" }
thiserror.workspace = true
memmap2.workspace = true
rayon.workspace = true
tracing.workspace = true
EOF
cat > crates/mora-esp/src/lib.rs <<'EOF'
//! Memory-mapped ESP/ESL/ESM reader, plugins.txt parser, and
//! record-type-indexed world view.
//!
//! Populated in milestone M2.
EOF
```

- [ ] **Step 4: Create the `mora-kid` stub**

Run:
```bash
mkdir -p crates/mora-kid/src
cat > crates/mora-kid/Cargo.toml <<'EOF'
[package]
name = "mora-kid"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "KID INI parser and distributor frontend for Mora."
publish = false

[dependencies]
mora-core = { path = "../mora-core" }
mora-esp = { path = "../mora-esp" }
thiserror.workspace = true
miette = { workspace = true }
tracing.workspace = true
EOF
cat > crates/mora-kid/src/lib.rs <<'EOF'
//! KID INI parser + `Distributor` implementation.
//!
//! Populated in milestone M3 (MVP: Weapon + Armor) and extended through
//! M6 (remaining record types) and M7 (distribution mode variants).
EOF
```

- [ ] **Step 5: Create the `mora-cli` stub**

Run:
```bash
mkdir -p crates/mora-cli/src
cat > crates/mora-cli/Cargo.toml <<'EOF'
[package]
name = "mora-cli"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "The `mora` compiler CLI."
publish = false

[[bin]]
name = "mora"
path = "src/main.rs"

[dependencies]
mora-core = { path = "../mora-core" }
mora-esp = { path = "../mora-esp" }
mora-kid = { path = "../mora-kid" }
anyhow.workspace = true
clap.workspace = true
miette.workspace = true
tracing.workspace = true
tracing-subscriber.workspace = true
EOF
cat > crates/mora-cli/src/main.rs <<'EOF'
//! `mora` CLI entry point.
//!
//! Populated in milestone M3 (wires the pipeline end-to-end for the
//! first time) and refined through M5 and M8.

fn main() {
    eprintln!("mora: not yet implemented (workspace stub — see M0 plan)");
    std::process::exit(2);
}
EOF
```

- [ ] **Step 6: Create the `mora-runtime` stub**

Run:
```bash
mkdir -p crates/mora-runtime/src
cat > crates/mora-runtime/Cargo.toml <<'EOF'
[package]
name = "mora-runtime"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "The MoraRuntime SKSE plugin DLL."
publish = false

[lib]
name = "MoraRuntime"
crate-type = ["cdylib"]

[dependencies]
mora-core = { path = "../mora-core" }
skse-rs = { path = "../skse-rs" }
memmap2.workspace = true
postcard.workspace = true
tracing.workspace = true
EOF
cat > crates/mora-runtime/src/lib.rs <<'EOF'
//! MoraRuntime SKSE plugin — loads mora_patches.bin and applies
//! patches at kDataLoaded.
//!
//! Populated in milestone M5.
EOF
```

- [ ] **Step 7: Create the `mora-test-harness` stub**

Run:
```bash
mkdir -p crates/mora-test-harness/src
cat > crates/mora-test-harness/Cargo.toml <<'EOF'
[package]
name = "mora-test-harness"
version.workspace = true
edition.workspace = true
license.workspace = true
repository.workspace = true
rust-version.workspace = true
description = "SKSE test-harness DLL exposing TCP commands for integration tests."
publish = false

[lib]
name = "MoraTestHarness"
crate-type = ["cdylib"]

[dependencies]
mora-core = { path = "../mora-core" }
skse-rs = { path = "../skse-rs" }
serde.workspace = true
serde_json = "1"
tracing.workspace = true
EOF
cat > crates/mora-test-harness/src/lib.rs <<'EOF'
//! MoraTestHarness SKSE plugin — opens TCP 127.0.0.1:9742, exposes
//! commands for bash-hook integration tests (`status`, `dump <type>`,
//! `lookup <fid>`, `quit`).
//!
//! Populated in milestone M5 (ports the existing C++ harness's protocol
//! to Rust).
EOF
```

- [ ] **Step 8: Create the `xtask` stub**

Run:
```bash
mkdir -p crates/xtask/src
cat > crates/xtask/Cargo.toml <<'EOF'
[package]
name = "xtask"
version.workspace = true
edition.workspace = true
license.workspace = true
rust-version.workspace = true
description = "Dev-workflow orchestration for Mora. Run via `cargo xtask <cmd>`."
publish = false

[dependencies]
anyhow.workspace = true
clap.workspace = true
EOF
cat > crates/xtask/src/main.rs <<'EOF'
//! `cargo xtask <cmd>` — dev-workflow orchestration.
//!
//! Commands are added as milestones require them:
//!   - M4: `capture-kid-goldens`
//!   - M5: `stage-runner-image`
//!   - later: `build-windows-dlls` and similar
//!
//! For now this is a stub that prints available commands (none yet).

use anyhow::Result;

fn main() -> Result<()> {
    eprintln!("xtask: no commands implemented yet (M0 stub)");
    std::process::exit(0);
}
EOF
```

- [ ] **Step 9: Commit**

Run:
```bash
git add crates/
git commit -m "build: add eight workspace crate stubs

Each crate has a Cargo.toml + minimal lib.rs/main.rs that compiles.
Real implementations arrive in later milestones:
  M1  skse-rs
  M2  mora-core, mora-esp
  M3  mora-kid, mora-cli (first end-to-end)
  M5  mora-runtime, mora-test-harness
  M4+ xtask commands added per need"
```

---

### Task 6: Verify the workspace builds end-to-end

**Files:** none modified; this is a verification task.

- [ ] **Step 1: `cargo check` the full workspace**

Run:
```bash
cargo check --workspace --all-targets
```

Expected: no errors. Warnings for unused deps in stub crates are acceptable. If any crate fails to compile, fix the offending Cargo.toml/lib.rs and re-run before moving on.

- [ ] **Step 2: `cargo build` to produce real artifacts**

Run:
```bash
cargo build --workspace
```

Expected: `target/debug/mora` binary exists, `target/debug/libMoraRuntime.so` (on Linux) and `target/debug/libMoraTestHarness.so` exist as cdylibs. No errors.

- [ ] **Step 3: `cargo test --workspace`**

Run:
```bash
cargo test --workspace
```

Expected: zero tests run, zero tests fail. This confirms the test harness is wired up even though no crate has tests yet.

- [ ] **Step 4: `cargo fmt --check` and `cargo clippy`**

Run:
```bash
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
```

Expected: both succeed. If fmt complains, run `cargo fmt` then re-run the check. If clippy complains, address the lint or add a targeted `#[allow(...)]` with a justifying comment.

- [ ] **Step 5: Attempt the Windows cross-compile**

Run:
```bash
cargo install cargo-xwin --locked
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: check passes for all crates. The two cdylibs (`mora-runtime`, `mora-test-harness`) will later be the primary Windows targets; the other crates build for Windows too, which is fine.

If `cargo xwin` fails with "xwin not installed", let it splat the SDK on first run — it caches under `~/.cache/cargo-xwin`. This first-run download is ~2GB and takes a few minutes.

- [ ] **Step 6: No commit — verification only**

Nothing to commit at this step. If the verification failed, fix the underlying crate stubs before proceeding to Phase C.

---

## Phase C — CI Pipeline (Tasks 7-8)

### Task 7: Rewrite `.github/workflows/ci.yml` for cargo

**Files:**
- Modify (complete rewrite): `.github/workflows/ci.yml`

The new CI uses GitHub-hosted `ubuntu-latest` runners for the fast path (cargo test/clippy/fmt/check + Windows cross-compile). The self-hosted Skyrim pool stays defined but its matrix job is gated on a path-existence check (only runs when `tests/integration/<case>/` has a Rust-compatible harness set up — which won't happen until M5).

- [ ] **Step 1: Overwrite `.github/workflows/ci.yml`**

Run:
```bash
cat > .github/workflows/ci.yml <<'EOF'
name: CI

on:
  push:
    branches: [master, rust-kid-pivot]
    tags: ['v*']
  pull_request:
  workflow_dispatch:

concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: true

env:
  CARGO_TERM_COLOR: always
  RUSTFLAGS: "-D warnings"

jobs:
  fmt:
    name: Fmt
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@stable
        with:
          components: rustfmt
      - run: cargo fmt --check

  clippy:
    name: Clippy
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@stable
        with:
          components: clippy
      - uses: Swatinem/rust-cache@v2
      - run: cargo clippy --workspace --all-targets -- -D warnings

  test:
    name: Test
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@stable
      - uses: Swatinem/rust-cache@v2
      - run: cargo test --workspace --all-targets

  windows-cross:
    name: Windows cross-compile (cargo-xwin)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: dtolnay/rust-toolchain@stable
        with:
          targets: x86_64-pc-windows-msvc
      - uses: Swatinem/rust-cache@v2
      - name: Install cargo-xwin
        run: cargo install cargo-xwin --locked
      - name: Cross-compile check
        run: cargo xwin check --target x86_64-pc-windows-msvc --workspace

  skyrim-integration:
    # Gated until M5 wires a Rust harness into tests/integration/.
    # A case is "Rust-ready" when it contains a Rust-compatible check.sh
    # plus the required KID INIs instead of .mora sources. Until at least
    # one case meets that bar, this job is skipped.
    name: Skyrim integration (self-hosted)
    runs-on: [self-hosted, linux, unraid, skyrim]
    if: ${{ hashFiles('tests/integration/**/rust-ready.marker') != '' }}
    needs: [test, windows-cross]
    steps:
      - uses: actions/checkout@v4
      - name: Build Linux artifacts
        run: cargo build --release --workspace
      - name: Cross-compile Windows DLLs
        run: cargo xwin build --release --target x86_64-pc-windows-msvc -p mora-runtime -p mora-test-harness
      - name: Placeholder — full orchestration lands in M5
        run: |
          echo "Skyrim integration pipeline will be implemented in M5."
          echo "When implemented, this step will iterate rust-ready cases,"
          echo "stage mod-dirs, invoke /usr/local/bin/run-skyrim-test.sh,"
          echo "and assert via check.sh hooks."
EOF
```

Rationale:
- Four always-on jobs (fmt/clippy/test/windows-cross) on GitHub-hosted runners keep PR CI independent of the private Unraid pool.
- `skyrim-integration` stays defined so we don't have to edit CI infrastructure in M5 — we just add a `rust-ready.marker` file inside the first Rust-ready case directory and the job activates automatically.
- `Swatinem/rust-cache@v2` caches `~/.cargo` + `target/` for dramatic speedups on repeat runs.
- `RUSTFLAGS: -D warnings` at the env level fails any build with a warning, matching clippy's strictness.

- [ ] **Step 2: Commit**

Run:
```bash
git add .github/workflows/ci.yml
git commit -m "ci: rewrite workflow for cargo workspace

Replaces the xmake + gtest + clang-cl + gtest-integration pipeline
with four GitHub-hosted jobs (fmt/clippy/test/windows-cross) plus a
self-hosted Skyrim integration job gated on a rust-ready.marker file
inside at least one tests/integration/<case>/ directory. The gate
stays closed until M5 wires the Rust harness in."
```

---

### Task 8: Verify CI locally with `act` (optional dry-run)

**Files:** none modified.

- [ ] **Step 1: Run the fmt + clippy + test jobs as they will run in CI**

Run:
```bash
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace --all-targets
```

Expected: all three pass cleanly. This replicates three of the four GitHub-hosted jobs; the fourth (`windows-cross`) was verified in Task 6 Step 5.

- [ ] **Step 2: Optional — push the branch and inspect the live workflow run**

Run:
```bash
git push -u origin rust-kid-pivot
```

Expected: CI runs on the push. Visit the Actions tab on GitHub; `fmt`, `clippy`, `test`, and `windows-cross` should all succeed. `skyrim-integration` is skipped (no `rust-ready.marker` files exist).

If any job fails, fix locally and push again. This step is optional but highly recommended — catches CI-environment-specific issues (missing tools, path differences) before they affect future PRs.

- [ ] **Step 3: No commit — verification only**

---

## Phase D — Documentation Scaffolding (Tasks 9-10)

### Task 9: Create the KID grammar compatibility matrix skeleton

**Files:**
- Create: `docs/src/kid-grammar.md`

This is the "what Mora supports" source of truth referenced throughout the spec. It starts empty (every cell `❌`) and fills in per-PR as M3/M6/M7 land. This early creation means future PRs have a concrete place to update rather than debating where it should live.

- [ ] **Step 1: Write `docs/src/kid-grammar.md`**

Run:
```bash
cat > docs/src/kid-grammar.md <<'EOF'
# KID Grammar Compatibility Matrix

This page is the public source of truth for which KID INI features
Mora currently supports. Each cell in the table is one of:

- ✅ — fully supported; golden-tested against real KID output
- 🟡 — supported but not yet golden-tested, or with caveats
- ❌ — not yet supported
- — — not applicable to this record type

Cells move left-to-right (❌ → 🟡 → ✅) as milestones land:

- **M3** ships Weapon + Armor × all filter types (first green cells).
- **M6** adds remaining record types (Ammo, Ingredient, …, Ingestible).
- **M7** adds the rarer distribution-mode variants and edge-case traits.

See `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md` for
the overall rollout plan.

## Record types × filter families

| Record type           | Form filter | Keyword filter | String filter | Trait filter | Chance |
|-----------------------|-------------|----------------|---------------|--------------|--------|
| Weapon                | ❌          | ❌             | ❌            | ❌           | ❌     |
| Armor                 | ❌          | ❌             | ❌            | ❌           | ❌     |
| Ammo                  | ❌          | ❌             | ❌            | ❌           | ❌     |
| MagicEffect           | ❌          | ❌             | ❌            | ❌           | ❌     |
| Potion                | ❌          | ❌             | ❌            | ❌           | ❌     |
| Scroll                | ❌          | ❌             | ❌            | ❌           | ❌     |
| Location              | ❌          | ❌             | ❌            | ❌           | ❌     |
| Ingredient            | ❌          | ❌             | ❌            | ❌           | ❌     |
| Book                  | ❌          | ❌             | ❌            | ❌           | ❌     |
| MiscItem              | ❌          | ❌             | ❌            | ❌           | ❌     |
| Key                   | ❌          | ❌             | ❌            | ❌           | ❌     |
| SoulGem               | ❌          | ❌             | ❌            | ❌           | ❌     |
| SpellItem             | ❌          | ❌             | ❌            | ❌           | ❌     |
| Activator             | ❌          | ❌             | ❌            | ❌           | ❌     |
| Flora                 | ❌          | ❌             | ❌            | ❌           | ❌     |
| Furniture             | ❌          | ❌             | ❌            | ❌           | ❌     |
| Race                  | ❌          | ❌             | ❌            | ❌           | ❌     |
| TalkingActivator      | ❌          | ❌             | ❌            | ❌           | ❌     |
| Enchantment           | ❌          | ❌             | ❌            | ❌           | ❌     |
| Ingestible            | ❌          | ❌             | ❌            | ❌           | ❌     |

## Distribution modes

| Mode                  | Status |
|-----------------------|--------|
| Regular               | ❌     |
| DistributeByWeight    | ❌     |
| DistributeByValue     | ❌     |

## Boolean composition

| Operator | Meaning           | Status |
|----------|-------------------|--------|
| `-`      | NOT               | ❌     |
| `\|`     | OR                | ❌     |
| `,`      | AND               | ❌     |

## Trait-filter coverage (per record type)

Placeholder — the trait inventory is record-type-specific and will be
filled in during M3 (Weapon/Armor) and M6 (remainder). Reference
`LookupFilters.cpp` in the upstream KID repository for the full set.
EOF
```

- [ ] **Step 2: Commit**

Run:
```bash
git add docs/src/kid-grammar.md
git commit -m "docs: add KID grammar compatibility matrix skeleton

Every cell ❌ at M0; fills in per-PR as M3/M6/M7 land. Serves as the
public 'what Mora supports' source of truth."
```

---

### Task 10: Create the M0 architecture snapshot doc

**Files:**
- Create: `docs/src/architecture.md`

This captures the current structural state so M1+ contributors have a lightweight orientation doc that isn't the full spec.

- [ ] **Step 1: Write `docs/src/architecture.md`**

Run:
```bash
cat > docs/src/architecture.md <<'EOF'
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
EOF
```

- [ ] **Step 2: Commit**

Run:
```bash
git add docs/src/architecture.md
git commit -m "docs: add M0 architecture snapshot

Lightweight orientation doc covering workspace layout, crate
dependency graph, and data flow. Points at the full spec for details."
```

---

## Phase E — Runner Image Refresh (Task 11)

### Task 11: Document the Unraid runner-image refresh

**Files:**
- Create: `docs/src/runner-image-refresh.md`

The Unraid-hosted self-hosted runner image at
`root@10.20.77.32:/mnt/user/skyrim-runner/` needs the C++ toolchain
swapped for Rust. This is a manual, out-of-repo task — but the plan
captures exactly what changes so it's reproducible.

The image is used by the `skyrim-integration` CI job. Since that job
is gated off until M5, the image refresh is technically M0-optional —
but doing it now, while we're touching the CI surface, prevents a
"dependency left behind" moment in M5.

- [ ] **Step 1: Write `docs/src/runner-image-refresh.md`**

Run:
```bash
cat > docs/src/runner-image-refresh.md <<'EOF'
# Unraid Runner Image Refresh (M0)

The self-hosted Skyrim runner image at
`root@10.20.77.32:/mnt/user/skyrim-runner/` was baked for the C++
pipeline (xmake + clang-cl + xwin + Proton). The Rust pivot replaces
the C++ cross-compile chain with `cargo-xwin`; this doc captures the
exact delta.

## What stays

- `/skyrim-base/` — vanilla GOG Skyrim SE 1.6.x + all CC content (bind-mount, read-only).
- `/skyrim-baseline/` — SKSE + Address Library `.bin` files (bind-mount, read-only).
- Proton-GE 10-34 at `/opt/proton/` + warm wine prefix at `/opt/warm-prefix/`.
- Display + audio stubs: Xorg with `xserver-xorg-video-dummy`, pulseaudio null sink, DXVK via nvidia-container-toolkit.
- `/usr/local/bin/run-skyrim-test.sh` orchestrator (language-agnostic; no changes needed).
- `/usr/local/bin/skyrim-env.sh` env helper (idempotent; no changes needed).
- Runner labels: `self-hosted, linux, unraid, skyrim`.

## What to remove

```bash
# Xmake toolchain
apt-get remove --purge -y xmake
rm -rf /opt/xmake ~/.xmake

# C++ cross-compile toolchain
apt-get remove --purge -y gcc-13 g++-13 clang-19 lld-19
rm -rf /opt/xwin

# C++ headers previously installed for the xmake builds
rm -rf /usr/include/fmt /usr/include/spdlog
```

## What to add

```bash
# Rust toolchain (rustup-managed, Rust 1.85).
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
  sh -s -- -y --default-toolchain 1.85 \
    --profile minimal \
    --component rustfmt,clippy
source "$HOME/.cargo/env"
rustup target add x86_64-pc-windows-msvc

# cargo-xwin for MSVC cross-compile from Linux.
cargo install cargo-xwin --locked

# Sanity: warm the xwin cache so the first real build doesn't
# download ~2GB of SDK.
cargo xwin --help >/dev/null
```

## Verification

From inside the running container:

```bash
rustc --version                          # expect: rustc 1.85.x (…)
cargo --version                          # expect: cargo 1.85.x (…)
cargo xwin --version                     # expect: cargo-xwin 0.x.y
rustup target list --installed           # expect: x86_64-pc-windows-msvc listed
```

Then clone the mora repo and run:

```bash
cd /tmp
git clone https://github.com/halgari/mora.git
cd mora
cargo check --workspace
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Both should succeed.

## Rollout

1. Stop the existing runner containers on Unraid.
2. Apply the "remove" + "add" steps above to the image Dockerfile (or
   whichever bake process is in use).
3. Rebuild and redeploy the containers.
4. Dispatch the Mora `ci` workflow manually (`workflow_dispatch`) and
   confirm the `test` / `clippy` / `fmt` / `windows-cross` jobs pick up
   the new image cleanly. The `skyrim-integration` job stays skipped
   until M5.

The image refresh is a prerequisite for M5 (it's where the Skyrim
integration pipeline actually runs), but doing it now keeps the infra
delta bundled with the rest of the M0 toolchain churn.
EOF
```

- [ ] **Step 2: Commit**

Run:
```bash
git add docs/src/runner-image-refresh.md
git commit -m "docs: document Unraid runner image refresh for Rust toolchain

Captures the C++ toolchain removal + Rust/cargo-xwin installation as
a reproducible delta. Prerequisite for M5; documented at M0 so the
infra change is bundled with the rest of the toolchain pivot."
```

- [ ] **Step 3: Perform the runner image refresh**

This is the only manual, out-of-repo step in the plan. SSH into the
Unraid box and follow `docs/src/runner-image-refresh.md` exactly.
When the verification commands at the bottom of that doc pass, the
runner is ready for M5. If you do not have access to the Unraid box,
coordinate with the person who does before starting M5.

No code commit for this step.

---

## Phase F — Final Verification & Handoff (Task 12)

### Task 12: Final sanity check + push + open tracking PR

**Files:** none modified.

- [ ] **Step 1: Full clean build from scratch**

Run:
```bash
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all six commands succeed with no warnings (or only warnings we've explicitly `#[allow]`-ed).

- [ ] **Step 2: Push the branch if not already pushed**

Run:
```bash
git push -u origin rust-kid-pivot
```

Expected: branch pushed, CI runs all four GitHub-hosted jobs and they all pass.

- [ ] **Step 3: Inspect `git log` to confirm the commit sequence**

Run:
```bash
git log --oneline origin/master..HEAD
```

Expected: roughly eight commits on `rust-kid-pivot`, in this order:

1. `chore: wipe C++ Datalog tree for Rust + KID pivot`
2. `docs: rewrite README as pivot notice with milestone status table`
3. `build: cargo workspace root manifest + toolchain + rustfmt/clippy config`
4. `build: add eight workspace crate stubs`
5. `ci: rewrite workflow for cargo workspace`
6. `docs: add KID grammar compatibility matrix skeleton`
7. `docs: add M0 architecture snapshot`
8. `docs: document Unraid runner image refresh for Rust toolchain`

Commit titles may differ slightly if tasks were split further, but the
overall shape should match.

- [ ] **Step 4: Open a PR from `rust-kid-pivot` → `master`**

The PR replaces the entire project's working state. Its description
should reference:
- The pivot spec: `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`
- This plan: `docs/superpowers/plans/2026-04-20-rust-kid-pivot-plan-1-foundation.md`
- The legacy tag: `legacy-cpp-pre-pivot`

Run:
```bash
gh pr create --base master --head rust-kid-pivot --title "Rust + KID pivot — M0: foundation" --body "$(cat <<'PRBODY'
## Summary

Implements M0 of the Rust + KID pivot per the spec at
`docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md` and the
plan at `docs/superpowers/plans/2026-04-20-rust-kid-pivot-plan-1-foundation.md`.

- Wipes the entire C++ Datalog tree (archived at tag `legacy-cpp-pre-pivot`).
- Adds a Rust cargo workspace with eight crate stubs that all compile.
- Replaces xmake + clang-tidy CI with cargo-native CI (fmt / clippy / test / Windows cross-compile via cargo-xwin).
- Scaffolds docs: KID grammar compatibility matrix, M0 architecture snapshot, Unraid runner image refresh procedure.

No functional code in any crate yet — that's M1.

## Test plan

- [x] `cargo check --workspace` succeeds on Linux.
- [x] `cargo test --workspace` succeeds (zero tests to run).
- [x] `cargo fmt --check` is clean.
- [x] `cargo clippy --workspace --all-targets -- -D warnings` is clean.
- [x] `cargo xwin check --target x86_64-pc-windows-msvc --workspace` succeeds.
- [ ] Self-hosted Unraid runner image refreshed per `docs/src/runner-image-refresh.md` (manual; required before M5).
- [ ] CI workflow runs cleanly on the PR.

## Next up

M1 plan — `skse-rs` minimal viable — gets written once this lands.
PRBODY
)"
```

Expected: PR is open against `master` with all four GitHub-hosted CI
jobs running. Link the PR URL somewhere you'll find it.

- [ ] **Step 5: Wait for CI + merge when green**

No commit here; this is the human merge gate. Once CI is green and the
runner-image refresh (Task 11 Step 3) is complete, merge the PR to
`master`. The pivot is officially under way.

---

## Completion criteria

All boxes below must be checked before calling M0 done and writing the
M1 plan:

- [ ] `legacy-cpp-pre-pivot` tag exists on origin.
- [ ] `master` on origin has the wiped-and-rescaffolded tree.
- [ ] `cargo check --workspace` and `cargo test --workspace` both succeed on a fresh clone.
- [ ] GitHub Actions `ci.yml` runs fmt/clippy/test/windows-cross and all four pass.
- [ ] Unraid runner image has Rust 1.85 + cargo-xwin installed; `cargo check` + `cargo xwin check` run there successfully.
- [ ] `docs/src/kid-grammar.md` exists with the empty matrix.
- [ ] `docs/src/architecture.md` exists with the current state snapshot.
- [ ] README.md reflects the pivot-in-progress status.

## Next plan

Once this plan is merged and the completion criteria above are met,
write plan #2: **`skse-rs` minimal viable (M1)**. That plan will benefit
from observing what the M0 workspace actually built — in particular,
whether any of the stub Cargo.toml entries need to be adjusted before
skse-rs starts depending on Windows-only APIs (via `windows-sys`) and
before it begins defining `#[repr(C)]` game struct layouts.

Plans for M2–M8 get written in sequence, each one just-in-time as the
prior milestone lands.
