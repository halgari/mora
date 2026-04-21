//! `cargo xtask capture-kid-goldens` — capture real-KID ground-truth
//! dumps for the M4 golden-test corpus.
//!
//! See `docs/superpowers/specs/2026-04-21-kid-golden-test-harness-design.md`.

use anyhow::{Result, bail};
use clap::Parser;
use std::path::PathBuf;

pub mod manifest;
pub mod staging;

#[derive(Parser, Debug)]
pub struct Args {
    /// Capture a single scenario by name.
    #[arg(long, conflicts_with = "all")]
    pub scenario: Option<String>,

    /// Capture every scenario found under `tests/golden-data/kid-inis/`.
    #[arg(long, conflicts_with = "scenario")]
    pub all: bool,

    /// Skyrim install root. Defaults to `/tmp/skyrim` (the overlay
    /// path set up by `run-skyrim-test.sh` on the runner image) when
    /// unset.
    #[arg(long, env = "SKYRIM_ROOT")]
    pub skyrim_root: Option<PathBuf>,
}

pub fn run(args: Args) -> Result<()> {
    // Validation: exactly one of --scenario / --all.
    let scenarios: Vec<String> = match (args.scenario.as_deref(), args.all) {
        (Some(name), false) => vec![name.to_string()],
        (None, true) => discover_scenarios()?,
        _ => bail!("exactly one of --scenario NAME or --all must be specified"),
    };

    // Task 7 fills this in.
    for name in &scenarios {
        println!("capture-kid-goldens: would capture {name}");
    }
    Ok(())
}

pub fn discover_scenarios() -> Result<Vec<String>> {
    let root = workspace_root()?.join("tests/golden-data/kid-inis");
    if !root.is_dir() {
        bail!(
            "scenario root not found: {} (run from a mora checkout)",
            root.display()
        );
    }
    let mut out = Vec::new();
    for entry in std::fs::read_dir(&root)? {
        let entry = entry?;
        if entry.file_type()?.is_dir()
            && let Some(name) = entry.file_name().to_str()
        {
            out.push(name.to_string());
        }
    }
    out.sort();
    Ok(out)
}

/// Find the workspace root by walking up from the current exe's
/// manifest-relative location. `CARGO_MANIFEST_DIR` at build time
/// points at `crates/xtask`; its parent-parent is the workspace root.
pub fn workspace_root() -> Result<PathBuf> {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let p = PathBuf::from(manifest_dir)
        .parent()
        .and_then(|p| p.parent())
        .ok_or_else(|| anyhow::anyhow!("cannot derive workspace root from {manifest_dir}"))?
        .to_path_buf();
    Ok(p)
}
