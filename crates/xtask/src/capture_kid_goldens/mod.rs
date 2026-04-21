//! `cargo xtask capture-kid-goldens` — capture real-KID ground-truth
//! dumps for the M4 golden-test corpus.
//!
//! See `docs/superpowers/specs/2026-04-21-kid-golden-test-harness-design.md`.

use anyhow::{Context, Result, bail};
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
    let scenarios: Vec<String> = match (args.scenario.as_deref(), args.all) {
        (Some(name), false) => vec![name.to_string()],
        (None, true) => discover_scenarios()?,
        _ => bail!("exactly one of --scenario NAME or --all must be specified"),
    };

    let root = workspace_root()?;
    let harness_dll = root.join("target/x86_64-pc-windows-msvc/release/MoraGoldenHarness.dll");
    let kid_dll = root.join("third_party/kid/KID.dll");
    let kid_ini = root.join("third_party/kid/KID.ini");
    let skyrim_root = args
        .skyrim_root
        .clone()
        .unwrap_or_else(|| std::path::PathBuf::from("/tmp/skyrim"));

    for name in &scenarios {
        capture_one_scenario(&root, name, &harness_dll, &kid_dll, &kid_ini, &skyrim_root)?;
    }
    println!("captured {}/{} scenarios", scenarios.len(), scenarios.len());
    Ok(())
}

fn capture_one_scenario(
    root: &std::path::Path,
    name: &str,
    harness_dll: &std::path::Path,
    kid_dll: &std::path::Path,
    kid_ini: &std::path::Path,
    skyrim_root: &std::path::Path,
) -> Result<()> {
    eprintln!("[capture] === {name} ===");
    let scenario_ini_dir = root.join("tests/golden-data/kid-inis").join(name);
    let stage = tempfile::tempdir()?;

    staging::assemble_mod_dir(&staging::AssembleInputs {
        output: stage.path(),
        kid_dll,
        kid_ini,
        scenario_ini_dir: &scenario_ini_dir,
        harness_dll,
    })?;

    // Emit a check.sh that waits for the harness's .done sentinel.
    let check_sh = stage.path().join("check.sh");
    std::fs::write(&check_sh, CHECK_SH_TEMPLATE)?;
    let mut perms = std::fs::metadata(&check_sh)?.permissions();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        perms.set_mode(0o755);
    }
    std::fs::set_permissions(&check_sh, perms)?;

    // Invoke run-skyrim-test.sh with the staged mod-dir and hook.
    let status = std::process::Command::new("/usr/local/bin/run-skyrim-test.sh")
        .arg(stage.path())
        .env("TEST_HOOK", &check_sh)
        .status()
        .with_context(|| {
            "running /usr/local/bin/run-skyrim-test.sh — on a local box, source \
             the runner image's helpers or install the script"
        })?;
    if !status.success() {
        bail!("run-skyrim-test.sh failed for scenario {name}: {status}");
    }

    // The dumps live at $SKYRIM_ROOT/Data/MoraCache/dumps/ at run time.
    // run-skyrim-test.sh stages $SKYRIM_ROOT under the path passed here
    // (default /tmp/skyrim on the runner image).
    let dump_src = skyrim_root.join("Data").join("MoraCache").join("dumps");
    if !dump_src.is_dir() {
        bail!(
            "dump directory not found at {} after run-skyrim-test.sh succeeded \
             — the harness DLL did not write to $SKYRIM_ROOT/Data/MoraCache/dumps/. \
             Check the harness crash via logs in $LOG_DIR.",
            dump_src.display()
        );
    }
    let expected_out = root.join("tests/golden-data/expected").join(name);
    std::fs::create_dir_all(&expected_out)?;
    for f in ["weapons.jsonl", "armors.jsonl"] {
        let src = dump_src.join(f);
        let dst = expected_out.join(f);
        std::fs::copy(&src, &dst)
            .with_context(|| format!("copying {} -> {}", src.display(), dst.display()))?;
    }

    // Manifest: pin the capture to the specific Skyrim data dir. Task 13's
    // test helper re-hashes the same files via MORA_SKYRIM_DATA and skips
    // if hashes diverge.
    let data_dir = std::env::var("MORA_SKYRIM_DATA")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| std::path::PathBuf::from("/skyrim-base/Data"));
    manifest::write_for_scenario(&expected_out, kid_dll, &data_dir)?;

    eprintln!("[capture] {name}: OK");
    Ok(())
}

const CHECK_SH_TEMPLATE: &str = r#"#!/usr/bin/env bash
# Wait up to 10 minutes for the harness to signal completion, then exit.
set -uo pipefail
SENTINEL="${SKYRIM_ROOT:-/tmp/skyrim}/Data/MoraCache/dumps/.done"
for _ in $(seq 1 600); do
    if [ -f "$SENTINEL" ]; then
        echo "[capture-check] sentinel detected"
        exit 0
    fi
    sleep 1
done
echo "[capture-check] timeout waiting for sentinel at $SENTINEL" >&2
exit 1
"#;

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
