# Rust + KID Pivot — Plan 7: `mora-cli` end-to-end compile (M3 Part 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** First real end-to-end `mora compile` — user points Mora at a Skyrim Data dir + plugins.txt and gets a `mora_patches.bin` on disk. Integrates everything built in Plans 2-6: mora-esp opens the world, mora-kid parses INIs + runs the distributor, mora-core serializes the patch file.

**Architecture:** `mora-cli` becomes a working binary with a `compile` subcommand. Explicit `--data-dir` and `--plugins-txt` flags (auto-detection deferred — Linux Proton paths + Windows registry add complexity that can wait for a follow-up). Output defaults to `<data-dir>/SKSE/Plugins/mora_patches.bin`. Errors render via `miette`. Stats printed to stderr. Adds a blake3-based load-order-hash helper to `mora-esp` so `PatchFile::load_order_hash` is populated correctly.

**Tech Stack:** Rust 1.90. `clap` (workspace-pinned, derive feature), `anyhow`, `miette`, `tracing`, `tracing-subscriber` (all already workspace deps). New `blake3` usage in `mora-esp` for load-order hashing (already a workspace dep from Plan 0).

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope discipline:**
- **`mora compile` only.** `mora check` and `mora info` subcommands deferred.
- **Explicit `--data-dir` and `--plugins-txt` flags.** Auto-detection (Linux Steam paths, Windows registry) is a follow-up.
- **Output defaults to `<data-dir>/SKSE/Plugins/mora_patches.bin`.** `--output` overrides. `--dry-run` skips writes.
- **No auto-install magic.** User points at their own Data dir.
- **No MO2 integration.** MO2 users point at their profile's Data + plugins.txt.

---

## File Structure

**Modified:**
- `crates/mora-esp/Cargo.toml` — add `blake3` workspace dep
- `crates/mora-esp/src/lib.rs` — re-export new `load_order_hash` module
- `crates/mora-cli/Cargo.toml` — already has clap, anyhow, miette, tracing, tracing-subscriber (from Plan 0); confirm
- `crates/mora-cli/src/main.rs` — replace stub with real clap entry point

**Created:**
- `crates/mora-esp/src/load_order_hash.rs` — blake3 digest over the canonical load-order representation
- `crates/mora-cli/src/cli.rs` — clap argument definitions (Commands enum + CompileArgs)
- `crates/mora-cli/src/compile.rs` — `compile` subcommand orchestration
- `crates/mora-cli/src/logging.rs` — tracing-subscriber setup
- `crates/mora-cli/tests/compile_end_to_end.rs` — integration test: synthetic Skyrim dir + INIs + `mora compile` → verify patch file
- `docs/src/mora-cli-reference.md` — CLI UX reference

---

## Phase A — Reference doc + load-order hash (Tasks 1-2)

### Task 1: CLI UX reference doc

**Files:**
- Create: `docs/src/mora-cli-reference.md`

- [ ] **Step 1: Write the doc**

```bash
cd /home/tbaldrid/oss/mora
cat > docs/src/mora-cli-reference.md <<'EOF'
# `mora` CLI Reference

## Commands

### `mora compile`

Discover KID INIs in a Skyrim Data directory, parse rules against the
active plugins, produce `mora_patches.bin`.

**Usage:**

```
mora compile --data-dir <PATH> --plugins-txt <PATH> [--output <PATH>] [--dry-run] [--verbose]
```

**Flags:**

| Flag              | Required | Description                                                          |
|-------------------|----------|----------------------------------------------------------------------|
| `--data-dir`      | yes      | Skyrim's `Data/` directory (contains `*.esm`, `*.esp`, KID INIs).    |
| `--plugins-txt`   | yes      | Path to `plugins.txt` defining active load order.                    |
| `--output`        | no       | Output path. Default: `<data-dir>/SKSE/Plugins/mora_patches.bin`.    |
| `--dry-run`       | no       | Run the full pipeline but don't write the output file.               |
| `--verbose`       | no       | Enable debug-level logging.                                          |

**Exit codes:**

| Code | Meaning                                |
|------|----------------------------------------|
| 0    | Success                                |
| 1    | User-facing error (bad path, parse error, etc.) |
| 2    | Internal error (panic, bug)            |

**Typical output (stderr):**

```
Mora v0.1.0
[OK] Loaded plugins.txt: 247 active plugins
[OK] Opened EspWorld: 1,247,301 records across 247 plugins
[OK] Discovered 34 _KID.ini files
[OK] Parsed 1,892 rules
[OK] Distributed keywords: 83,428 patches emitted
[OK] Wrote mora_patches.bin (1.1 MB)
  load_order_hash: 0xdeadbeefcafef00d
  candidates considered: 247,103
  rejected by filter:    163,615
  rejected by chance:    60
  rules evaluated:       1,892
Total: 623ms
```

### Future commands (not implemented yet)

- `mora check` — parse INIs + validate without writing output
- `mora info` — dump load order + rule counts for debugging

## `mora_patches.bin` format

postcard-serialized `mora_core::PatchFile`. See
`docs/src/mora-core-reference.md` for the struct layout.

The `load_order_hash` field is a 64-bit blake3 digest over the
canonical load-order representation (plugin filenames lowercased,
joined by NUL, including their master lists). Runtime verifies this
on load; mismatch → refuse to apply.
EOF
```

- [ ] **Step 2: Commit**

```bash
git add docs/src/mora-cli-reference.md
git commit -m "docs: mora-cli reference

Documents the mora compile subcommand: flags, exit codes, typical
output format, and load_order_hash semantics. Future commands
(check, info) flagged as deferred."
```

---

### Task 2: `load_order_hash.rs` in mora-esp

**Files:**
- Modify: `crates/mora-esp/Cargo.toml`
- Modify: `crates/mora-esp/src/lib.rs`
- Create: `crates/mora-esp/src/load_order_hash.rs`

- [ ] **Step 1: Add blake3 dep to mora-esp**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/Cargo.toml")
text = p.read_text()
marker = "lz4_flex.workspace = true\n"
new_line = "blake3.workspace = true\n"
if "blake3" not in text:
    text = text.replace(marker, marker + new_line, 1)
    p.write_text(text)
PY
grep blake3 crates/mora-esp/Cargo.toml
```

- [ ] **Step 2: Write load_order_hash.rs**

```bash
cat > crates/mora-esp/src/load_order_hash.rs <<'EOF'
//! Load-order hashing for `mora_patches.bin` sanity check.
//!
//! The hash is a blake3 digest truncated to 64 bits over a
//! canonical representation of the active load order. The runtime
//! recomputes it and refuses to apply the patch file if it differs —
//! catches "user regenerated their load order without recompiling".

use crate::plugin::EspPlugin;

/// Compute a canonical 64-bit digest over the given plugin list.
///
/// Canonical form: for each plugin, append:
///   `<filename_lowercase>\0<esm_flag>\0<esl_flag>\0<master_count>\0<master_filenames_lowercase_nul_joined>\0`
///
/// The master list is part of the hash because a plugin updated with
/// new or reordered masters produces new runtime FormIDs even if the
/// plugin filename is unchanged.
pub fn compute_load_order_hash(plugins: &[EspPlugin]) -> u64 {
    let mut hasher = blake3::Hasher::new();
    for p in plugins {
        hasher.update(p.filename.to_ascii_lowercase().as_bytes());
        hasher.update(&[0]);
        hasher.update(&[p.is_esm() as u8]);
        hasher.update(&[0]);
        hasher.update(&[p.is_esl() as u8]);
        hasher.update(&[0]);
        hasher.update(&(p.header.masters.len() as u32).to_le_bytes());
        hasher.update(&[0]);
        for master in &p.header.masters {
            hasher.update(master.to_ascii_lowercase().as_bytes());
            hasher.update(&[0]);
        }
    }
    let digest = hasher.finalize();
    // Truncate to 64 bits (first 8 bytes).
    let bytes: [u8; 32] = digest.into();
    u64::from_le_bytes(bytes[..8].try_into().unwrap())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_plugin_list_hashes_consistently() {
        let a = compute_load_order_hash(&[]);
        let b = compute_load_order_hash(&[]);
        assert_eq!(a, b);
    }
}
EOF
```

Unit tests here are minimal because constructing `EspPlugin` instances without file I/O isn't straightforward — integration tests in Task 10 exercise the hash end-to-end.

- [ ] **Step 3: Register module**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-esp/src/lib.rs")
text = p.read_text()
if "pub mod load_order_hash" not in text:
    text = text.replace("pub mod load_order;", "pub mod load_order;\npub mod load_order_hash;")
    p.write_text(text)
PY
grep load_order_hash crates/mora-esp/src/lib.rs
```

- [ ] **Step 4: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo test --package mora-esp --lib load_order_hash::tests
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-esp/
git commit -m "mora-esp: load_order_hash module

blake3-truncated-to-64-bit digest over a canonical load-order
representation (plugin filenames + ESM/ESL flags + master lists,
all lowercased and NUL-separated). Populates PatchFile::load_order_hash;
runtime verifies on load and refuses on mismatch."
```

---

## Phase B — CLI scaffold + logging (Tasks 3-4)

### Task 3: `mora-cli/src/cli.rs` — clap args

**Files:**
- Modify: `crates/mora-cli/Cargo.toml` (verify deps)
- Create: `crates/mora-cli/src/cli.rs`

- [ ] **Step 1: Verify `mora-cli/Cargo.toml` has the needed deps**

```bash
cd /home/tbaldrid/oss/mora
grep -E "(clap|anyhow|miette|tracing)" crates/mora-cli/Cargo.toml
```

Expected to see: `anyhow.workspace = true`, `clap.workspace = true`, `miette.workspace = true`, `tracing.workspace = true`, `tracing-subscriber.workspace = true`, plus `mora-core`, `mora-esp`, `mora-kid` path deps. Add `mora-kid` if missing:

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-cli/Cargo.toml")
text = p.read_text()
if 'mora-kid = { path' not in text:
    marker = 'mora-esp = { path = "../mora-esp" }\n'
    new_line = 'mora-kid = { path = "../mora-kid" }\n'
    text = text.replace(marker, marker + new_line)
    p.write_text(text)
PY
grep mora-kid crates/mora-cli/Cargo.toml
```

- [ ] **Step 2: Write cli.rs**

```bash
cat > crates/mora-cli/src/cli.rs <<'EOF'
//! Command-line argument definitions via clap derive.

use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser, Debug)]
#[command(name = "mora", version, about = "Mora — KID-compatible keyword distributor compiler")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand, Debug)]
pub enum Commands {
    /// Compile KID INIs against the active plugins into mora_patches.bin.
    Compile(CompileArgs),
}

#[derive(Debug, clap::Args)]
pub struct CompileArgs {
    /// Skyrim's Data/ directory. Contains *.esm, *.esp, and *_KID.ini files.
    #[arg(long, value_name = "PATH")]
    pub data_dir: PathBuf,

    /// Path to plugins.txt defining active load order.
    #[arg(long, value_name = "PATH")]
    pub plugins_txt: PathBuf,

    /// Output path for mora_patches.bin. Default: <data-dir>/SKSE/Plugins/mora_patches.bin.
    #[arg(long, value_name = "PATH")]
    pub output: Option<PathBuf>,

    /// Run the full pipeline but don't write the output file.
    #[arg(long)]
    pub dry_run: bool,

    /// Enable debug-level logging.
    #[arg(long, short)]
    pub verbose: bool,
}

impl CompileArgs {
    /// Resolve the output path, defaulting to <data_dir>/SKSE/Plugins/mora_patches.bin.
    pub fn resolve_output(&self) -> PathBuf {
        self.output
            .clone()
            .unwrap_or_else(|| self.data_dir.join("SKSE").join("Plugins").join("mora_patches.bin"))
    }
}
EOF
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-cli --all-targets
git add crates/mora-cli/
git commit -m "mora-cli: clap argument definitions

Cli { command: Commands::Compile(CompileArgs) }. CompileArgs exposes
--data-dir, --plugins-txt, --output, --dry-run, --verbose flags.
resolve_output() defaults to <data-dir>/SKSE/Plugins/mora_patches.bin."
```

---

### Task 4: `mora-cli/src/logging.rs` — tracing subscriber

**Files:**
- Create: `crates/mora-cli/src/logging.rs`

- [ ] **Step 1: Write logging.rs**

```bash
cat > crates/mora-cli/src/logging.rs <<'EOF'
//! Tracing subscriber setup for user-facing output.

use tracing_subscriber::EnvFilter;
use tracing_subscriber::fmt::format::FmtSpan;

/// Initialize tracing. `verbose=true` enables `debug` level; else `info`.
/// Respects `RUST_LOG` when set (overrides our default filter).
pub fn init(verbose: bool) {
    let default_filter = if verbose { "debug" } else { "info" };
    let env_filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new(default_filter));

    tracing_subscriber::fmt()
        .with_env_filter(env_filter)
        .with_target(false)
        .with_span_events(FmtSpan::NONE)
        .without_time()
        .with_writer(std::io::stderr)
        .init();
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-cli --all-targets
git add crates/mora-cli/src/logging.rs
git commit -m "mora-cli: tracing subscriber setup

Initializes an env-filter-aware subscriber with stderr output, no
timestamps, no targets, no spans — clean log lines for user-facing
output. Respects RUST_LOG; --verbose flips default to debug."
```

---

## Phase C — Compile orchestration (Tasks 5-6)

### Task 5: `mora-cli/src/compile.rs` — compile command

**Files:**
- Create: `crates/mora-cli/src/compile.rs`

- [ ] **Step 1: Write compile.rs**

```bash
cat > crates/mora-cli/src/compile.rs <<'EOF'
//! `mora compile` subcommand — end-to-end patch compilation.

use std::time::Instant;

use anyhow::{Context, Result};
use mora_core::{DeterministicChance, Distributor, PatchFile, PatchSink};
use mora_esp::EspWorld;
use mora_esp::load_order_hash::compute_load_order_hash;
use mora_kid::distributor::KidDistributor;
use mora_kid::ini;
use tracing::{info, warn};

use crate::cli::CompileArgs;

pub fn run(args: CompileArgs) -> Result<()> {
    let started = Instant::now();

    // Validate paths exist.
    anyhow::ensure!(
        args.data_dir.is_dir(),
        "--data-dir does not exist or is not a directory: {}",
        args.data_dir.display()
    );
    anyhow::ensure!(
        args.plugins_txt.is_file(),
        "--plugins-txt does not exist: {}",
        args.plugins_txt.display()
    );

    info!("Mora v{}", env!("CARGO_PKG_VERSION"));

    // Open the EspWorld.
    let world = EspWorld::open(&args.data_dir, &args.plugins_txt)
        .context("failed to open EspWorld")?;
    info!(
        "Loaded plugins.txt: {} active plugins",
        world.plugins.len()
    );

    // Compute load-order hash.
    let load_hash = compute_load_order_hash(&world.plugins);
    info!("Load-order hash: 0x{load_hash:016x}");

    // Discover + parse KID INIs.
    let ini_paths = ini::discover_kid_ini_files(&args.data_dir)
        .context("failed to scan for _KID.ini files")?;
    info!("Discovered {} _KID.ini files", ini_paths.len());

    let mut all_rules = Vec::new();
    for p in &ini_paths {
        match ini::parse_file(p) {
            Ok(rules) => all_rules.extend(rules),
            Err(e) => {
                warn!("{}: {}", p.display(), e);
            }
        }
    }
    info!("Parsed {} rules", all_rules.len());

    // Run distributor.
    let distributor = KidDistributor::new(all_rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    sink.set_load_order_hash(load_hash);
    let stats = distributor
        .lower(&world, &chance, &mut sink)
        .context("distributor failed")?;

    info!("Rules evaluated:        {}", stats.rules_evaluated);
    info!("Candidates considered:  {}", stats.candidates_considered);
    info!("Patches emitted:        {}", stats.patches_emitted);
    info!("Rejected by filter:     {}", stats.rejected_by_filter);
    info!("Rejected by chance:     {}", stats.rejected_by_chance);

    let file: PatchFile = sink.finalize();

    // Write (or skip on --dry-run).
    if args.dry_run {
        info!(
            "Dry run: would have written {} bytes to {}",
            file.to_bytes().unwrap_or_default().len(),
            args.resolve_output().display()
        );
    } else {
        let output_path = args.resolve_output();
        if let Some(parent) = output_path.parent() {
            std::fs::create_dir_all(parent)
                .with_context(|| format!("failed to create output dir: {}", parent.display()))?;
        }
        let bytes = file.to_bytes().context("postcard serialize failed")?;
        std::fs::write(&output_path, &bytes)
            .with_context(|| format!("failed to write {}", output_path.display()))?;
        info!(
            "Wrote mora_patches.bin: {} bytes → {}",
            bytes.len(),
            output_path.display()
        );
    }

    info!("Total: {}ms", started.elapsed().as_millis());
    Ok(())
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-cli --all-targets
cargo xwin check --package mora-cli --target x86_64-pc-windows-msvc
git add crates/mora-cli/src/compile.rs
git commit -m "mora-cli: compile subcommand orchestration

Opens EspWorld, computes load-order hash, discovers + parses KID INIs,
runs KidDistributor, writes mora_patches.bin (or skips on --dry-run).
anyhow::Context wraps every step with user-readable error messages."
```

---

### Task 6: `main.rs` wire-up

**Files:**
- Modify: `crates/mora-cli/src/main.rs`

- [ ] **Step 1: Replace stub main.rs**

```bash
cat > crates/mora-cli/src/main.rs <<'EOF'
//! `mora` CLI entry point.

mod cli;
mod compile;
mod logging;

use clap::Parser;

use crate::cli::{Cli, Commands};

fn main() {
    let args = Cli::parse();
    let verbose = match &args.command {
        Commands::Compile(c) => c.verbose,
    };
    logging::init(verbose);

    let result = match args.command {
        Commands::Compile(c) => compile::run(c),
    };

    match result {
        Ok(()) => std::process::exit(0),
        Err(e) => {
            // Print error chain to stderr.
            eprintln!("error: {e}");
            let mut source = e.source();
            while let Some(s) = source {
                eprintln!("  caused by: {s}");
                source = s.source();
            }
            std::process::exit(1);
        }
    }
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-cli --all-targets
cargo build --package mora-cli
./target/debug/mora --help 2>&1 | head -10
./target/debug/mora compile --help 2>&1 | head
git add crates/mora-cli/src/main.rs
git commit -m "mora-cli: main.rs — argv parse + dispatch + error chain

Parses args via clap, initializes logging, dispatches to the
requested subcommand. On error, prints the anyhow error chain to
stderr and exits with code 1."
```

---

## Phase D — Integration test (Tasks 7-8)

### Task 7: Fixture-building helper (reusable test setup)

**Files:**
- Create: `crates/mora-cli/tests/fixtures.rs`

- [ ] **Step 1: Write fixtures.rs (shared test setup)**

```bash
mkdir -p crates/mora-cli/tests
cat > crates/mora-cli/tests/fixtures.rs <<'EOF'
//! Shared test fixture helpers for mora-cli integration tests.
//!
//! Builds a synthetic Skyrim-ish directory layout with ESMs + KID INIs
//! + plugins.txt, then runs mora-cli against it.

#![allow(dead_code)]

use std::io::Write;
use std::path::{Path, PathBuf};

pub struct Fixture {
    pub data_dir: PathBuf,
    pub plugins_txt: PathBuf,
}

impl Fixture {
    /// Create a fresh tmpdir with the given (filename, bytes) plugins.
    /// Writes plugins.txt marking all as active.
    pub fn new(label: &str, plugins: &[(&str, Vec<u8>)]) -> Self {
        let dir = std::env::temp_dir()
            .join(format!("mora-cli-it-{label}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();

        for (name, bytes) in plugins {
            let path = dir.join(name);
            std::fs::File::create(&path).unwrap().write_all(bytes).unwrap();
        }

        let plugins_txt = dir.join("plugins.txt");
        let mut f = std::fs::File::create(&plugins_txt).unwrap();
        for (name, _) in plugins {
            writeln!(f, "*{name}").unwrap();
        }

        Fixture {
            data_dir: dir,
            plugins_txt,
        }
    }

    pub fn write_kid_ini(&self, name: &str, content: &str) -> PathBuf {
        let path = self.data_dir.join(name);
        std::fs::write(&path, content).unwrap();
        path
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.data_dir);
    }
}

/// Build a minimal plugin byte sequence with optional KYWD/WEAP/ARMO groups.
/// Lifted from the mora-kid test fixtures — lightweight reimplementation.
pub fn build_plugin(
    is_esm: bool,
    keywords: &[(&str, u32)],  // (editor_id, form_id)
    weapons: &[(u32, &str, Vec<u32>)],  // (form_id, editor_id, keyword_form_ids)
    armors: &[(u32, &str, Vec<u32>)],
) -> Vec<u8> {
    fn sub(sig: &[u8; 4], data: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(sig);
        v.extend_from_slice(&(data.len() as u16).to_le_bytes());
        v.extend_from_slice(data);
        v
    }
    fn rec(sig: &[u8; 4], form_id: u32, body: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(sig);
        v.extend_from_slice(&(body.len() as u32).to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // flags
        v.extend_from_slice(&form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&44u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(body);
        v
    }
    fn group(label: &[u8; 4], contents: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&((24 + contents.len()) as u32).to_le_bytes());
        v.extend_from_slice(label);
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(contents);
        v
    }
    fn nul_cstr(s: &str) -> Vec<u8> {
        let mut v = s.as_bytes().to_vec();
        v.push(0);
        v
    }
    fn kwda_payload(ids: &[u32]) -> Vec<u8> {
        let mut v = Vec::with_capacity(ids.len() * 4);
        for &id in ids {
            v.extend_from_slice(&id.to_le_bytes());
        }
        v
    }

    // TES4 header
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&(if is_esm { 1u32 } else { 0u32 }).to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);

    // KYWD group
    if !keywords.is_empty() {
        let mut contents = Vec::new();
        for (edid, form_id) in keywords {
            let body = sub(b"EDID", &nul_cstr(edid));
            contents.extend_from_slice(&rec(b"KYWD", *form_id, &body));
        }
        out.extend_from_slice(&group(b"KYWD", &contents));
    }

    // WEAP group
    if !weapons.is_empty() {
        let mut contents = Vec::new();
        for (form_id, edid, kwids) in weapons {
            let mut body = sub(b"EDID", &nul_cstr(edid));
            body.extend_from_slice(&sub(b"KWDA", &kwda_payload(kwids)));
            contents.extend_from_slice(&rec(b"WEAP", *form_id, &body));
        }
        out.extend_from_slice(&group(b"WEAP", &contents));
    }

    // ARMO group
    if !armors.is_empty() {
        let mut contents = Vec::new();
        for (form_id, edid, kwids) in armors {
            let mut body = sub(b"EDID", &nul_cstr(edid));
            body.extend_from_slice(&sub(b"KWDA", &kwda_payload(kwids)));
            contents.extend_from_slice(&rec(b"ARMO", *form_id, &body));
        }
        out.extend_from_slice(&group(b"ARMO", &contents));
    }

    out
}

/// Locate the mora binary built in target/debug/.
pub fn mora_bin() -> PathBuf {
    let workspace_root = env!("CARGO_MANIFEST_DIR");
    // CARGO_MANIFEST_DIR is crates/mora-cli — go up two to workspace root.
    Path::new(workspace_root)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("target/debug/mora")
}

#[test]
fn fixtures_compile() {
    let _ = build_plugin(true, &[], &[], &[]);
}
EOF
```

- [ ] **Step 2: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-cli --test fixtures
git add crates/mora-cli/tests/fixtures.rs
git commit -m "mora-cli: test fixture helpers

Fixture struct creates a tmpdir with synthetic plugins + plugins.txt,
cleans up on Drop. build_plugin() composes minimal ESM bytes with
optional KYWD/WEAP/ARMO groups. mora_bin() locates the built binary."
```

---

### Task 8: End-to-end integration test

**Files:**
- Create: `crates/mora-cli/tests/compile_end_to_end.rs`

- [ ] **Step 1: Write compile_end_to_end.rs**

```bash
cat > crates/mora-cli/tests/compile_end_to_end.rs <<'EOF'
//! End-to-end integration tests for `mora compile`.
//!
//! Each test builds a synthetic Skyrim Data dir, invokes the compiled
//! `mora` binary, and verifies the resulting patch file.

mod fixtures;

use std::process::Command;

use fixtures::*;
use mora_core::{FormId, Patch, PatchFile};

fn run_mora_compile(fixture: &Fixture, output: &std::path::Path, extra_args: &[&str]) -> std::process::Output {
    Command::new(mora_bin())
        .arg("compile")
        .arg("--data-dir")
        .arg(&fixture.data_dir)
        .arg("--plugins-txt")
        .arg(&fixture.plugins_txt)
        .arg("--output")
        .arg(output)
        .args(extra_args)
        .output()
        .expect("failed to run mora")
}

#[test]
fn compile_produces_valid_patch_file() {
    // Build synthetic world: one plugin with Iron keyword + Iron Sword.
    let bytes = build_plugin(
        true,
        &[("WeapMaterialIron", 0x0001_E718)],
        &[(0x0001_2EB7, "IronSword", vec![])],
        &[],
    );
    let fixture = Fixture::new("valid", &[("Test.esm", bytes)]);
    fixture.write_kid_ini("Test_KID.ini", "WeapMaterialIron = Weapon\n");

    let output = fixture.data_dir.join("mora_patches.bin");
    let out = run_mora_compile(&fixture, &output, &[]);

    assert!(
        out.status.success(),
        "mora compile failed: stdout={} stderr={}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
    assert!(output.is_file(), "patch file not created");

    let bytes = std::fs::read(&output).unwrap();
    let file = PatchFile::from_bytes(&bytes).expect("valid PatchFile");
    assert_eq!(&file.magic, b"MORA");
    assert_eq!(file.version, 1);
    assert_eq!(file.patches.len(), 1);
    assert!(matches!(
        file.patches[0],
        Patch::AddKeyword {
            target: FormId(0x0001_2EB7),
            keyword: FormId(0x0001_E718)
        }
    ));
}

#[test]
fn compile_dry_run_does_not_write() {
    let bytes = build_plugin(
        true,
        &[("WeapMaterialIron", 0x0001_E718)],
        &[(0x0001_2EB7, "IronSword", vec![])],
        &[],
    );
    let fixture = Fixture::new("dry", &[("Test.esm", bytes)]);
    fixture.write_kid_ini("Test_KID.ini", "WeapMaterialIron = Weapon\n");

    let output = fixture.data_dir.join("mora_patches.bin");
    let out = run_mora_compile(&fixture, &output, &["--dry-run"]);
    assert!(out.status.success());
    assert!(!output.exists(), "dry-run should not produce a file");
}

#[test]
fn compile_bad_data_dir_exits_nonzero() {
    // Use a path that exists as a FILE (not a dir) for data-dir.
    let tmpfile = std::env::temp_dir().join(format!("mora-not-a-dir-{}", std::process::id()));
    std::fs::write(&tmpfile, "").unwrap();
    let plugins_txt = std::env::temp_dir().join(format!("mora-bad-plugins-{}.txt", std::process::id()));
    std::fs::write(&plugins_txt, "*Doesnotmatter.esm\n").unwrap();

    let out = Command::new(mora_bin())
        .arg("compile")
        .arg("--data-dir")
        .arg(&tmpfile)
        .arg("--plugins-txt")
        .arg(&plugins_txt)
        .arg("--output")
        .arg(std::env::temp_dir().join("should-not-be-created.bin"))
        .output()
        .expect("run mora");
    assert!(!out.status.success(), "expected non-zero exit for bad data-dir");
    let stderr = String::from_utf8_lossy(&out.stderr);
    assert!(stderr.contains("data-dir"), "stderr should mention data-dir: {stderr}");

    let _ = std::fs::remove_file(&tmpfile);
    let _ = std::fs::remove_file(&plugins_txt);
}

#[test]
fn compile_empty_kid_ini_set_produces_empty_patches() {
    // No KID INIs at all → zero patches emitted, patch file still well-formed.
    let bytes = build_plugin(
        true,
        &[],
        &[(0x0001_2EB7, "IronSword", vec![])],
        &[],
    );
    let fixture = Fixture::new("empty-inis", &[("Test.esm", bytes)]);

    let output = fixture.data_dir.join("mora_patches.bin");
    let out = run_mora_compile(&fixture, &output, &[]);
    assert!(out.status.success());
    let file = PatchFile::from_bytes(&std::fs::read(&output).unwrap()).unwrap();
    assert!(file.patches.is_empty());
    assert_eq!(&file.magic, b"MORA");
}
EOF
```

- [ ] **Step 2: Run the tests**

Integration tests require `mora` built in `target/debug/`. Cargo builds it automatically when a test binary depends on it — BUT these tests don't declare a dependency on the binary, they invoke it as a subprocess. Force the build explicitly first.

```bash
source $HOME/.cargo/env
cargo build --package mora-cli
cargo test --package mora-cli --test compile_end_to_end -- --test-threads=1
```

`--test-threads=1` serializes to avoid tmpdir races (Plan 6 learned this).

- [ ] **Step 3: Commit**

```bash
git add crates/mora-cli/tests/
git commit -m "mora-cli: end-to-end integration tests

4 tests against a built mora binary + synthetic Skyrim dirs:
  compile_produces_valid_patch_file, compile_dry_run_does_not_write,
  compile_bad_data_dir_exits_nonzero, compile_empty_kid_ini_set.
Exercises the full pipeline: argv parse → EspWorld::open →
pipeline::compile → PatchFile serialization → on-disk bytes."
```

---

## Phase E — Final verification (Task 9)

### Task 9: Full clean verify + push + PR

**Files:** none modified.

- [ ] **Step 1: Clean verify**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo build --package mora-cli
cargo test --workspace --all-targets -- --test-threads=1
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all seven green. Test count: ~171 (167 from M3 + 4 new integration).

- [ ] **Step 2: Smoke-test the binary manually**

```bash
./target/debug/mora --help
./target/debug/mora compile --help
```

Expected: clap-rendered help text for both commands.

- [ ] **Step 3: Push + PR**

```bash
git push -u origin m3-mora-cli-compile
gh pr create --base master --head m3-mora-cli-compile \
    --title "Rust + KID pivot — M3 Part 2: mora-cli end-to-end compile" \
    --body "$(cat <<'PRBODY'
## Summary

Delivers the first real \`mora compile\`.

- \`mora\` binary with a \`compile\` subcommand via clap
- \`--data-dir\` + \`--plugins-txt\` required flags; \`--output\` optional (default \`<data-dir>/SKSE/Plugins/mora_patches.bin\`); \`--dry-run\` + \`--verbose\` modifiers
- Opens EspWorld, discovers + parses all \`*_KID.ini\` files, runs KidDistributor, writes \`mora_patches.bin\`
- \`load_order_hash\`: blake3 digest over the canonical load-order representation (plugin names + ESM/ESL flags + master lists, lowercased + NUL-separated), truncated to 64 bits; populates \`PatchFile::load_order_hash\` for runtime sanity checks
- \`docs/src/mora-cli-reference.md\` — CLI UX reference

## Test plan

- [x] \`cargo test --workspace\` — 171 tests pass (167 prior + 4 new integration)
- [x] \`cargo clippy --all-targets -- -D warnings\` clean
- [x] \`cargo fmt --check\` clean
- [x] \`cargo xwin check --target x86_64-pc-windows-msvc --workspace\` clean
- [x] \`./target/debug/mora --help\` and \`mora compile --help\` render

## Scope discipline

- **\`mora compile\` only.** \`mora check\` + \`mora info\` deferred.
- **Explicit flags** — no auto-detection of Skyrim install yet.
- **No auto-install.** User supplies \`--data-dir\` and \`--plugins-txt\`.
- **No MO2 integration.** MO2 users point at their profile's Data + plugins.txt.

## Next up

**Plan 8: activate deferred filters + trait predicates**. Mora currently
parses all KID filter buckets (NOT/MATCH/ALL/ANY) but only evaluates
NOT + MATCH. Plan 8 activates ALL + ANY, plus the trait predicates
that require subrecord data not yet exposed on WeaponRecord/ArmorRecord
(anim type, armor type, DNAM damage/weight ranges). After that,
Mora's KID coverage is complete for Weapon + Armor.
PRBODY
)"
```

- [ ] **Step 4: Watch CI + hand off**

---

## Completion criteria

- [ ] `mora compile` produces a valid `mora_patches.bin` from a synthetic Skyrim dir.
- [ ] 4 new integration tests pass; all 171 workspace tests pass.
- [ ] `load_order_hash` is non-zero and reproducible.
- [ ] PR merged to `master`.

## Next plan

**Plan 8: Complete KID parity on Weapon + Armor** — activate ALL/ANY
filter buckets, implement trait predicates by exposing anim-type /
armor-type / DNAM fields on `WeaponRecord` / `ArmorRecord`, handle
ExclusiveGroup. After Plan 8, Weapon + Armor rules hit the full KID
filter grammar.
