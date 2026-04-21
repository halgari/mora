//! Mod-dir staging: copy KID.dll + KID.ini + scenario INI +
//! MoraGoldenHarness.dll into a temp dir suitable for
//! `run-skyrim-test.sh`.

use anyhow::{Context, Result, bail};
use std::path::Path;

pub struct AssembleInputs<'a> {
    pub output: &'a Path,
    pub kid_dll: &'a Path,
    pub kid_ini: &'a Path,
    pub scenario_ini_dir: &'a Path,
    pub harness_dll: &'a Path,
}

/// Assemble a staged mod-dir at `inputs.output`. Resulting layout:
///
/// ```text
/// <output>/Data/
///   <scenario>_KID.ini      ← KID scans Data/ directly (NOT Data/SKSE/Plugins)
///   SKSE/Plugins/
///     KID.dll
///     KID.ini                 (KID's own global config — also under SKSE/Plugins)
///     MoraGoldenHarness.dll
/// ```
///
/// Only files that contain `_kid` AND end in `.ini` (case-insensitive)
/// are copied from the scenario directory; READMEs and other non-ini
/// fixtures are skipped. This matches `mora-kid::ini::discover_kid_ini_files`
/// and KID 3.4.0's `LookupConfigs.cpp` which explicitly scans `Data/`
/// (not `Data/SKSE/Plugins/`) for `_KID.ini` files.
pub fn assemble_mod_dir(inputs: &AssembleInputs<'_>) -> Result<()> {
    if !inputs.harness_dll.is_file() {
        bail!(
            "harness DLL not found at {} — did you `cargo xwin build --release --target x86_64-pc-windows-msvc -p mora-golden-harness` first?",
            inputs.harness_dll.display()
        );
    }
    if !inputs.kid_dll.is_file() {
        bail!("KID DLL not found at {}", inputs.kid_dll.display());
    }
    if !inputs.kid_ini.is_file() {
        bail!("KID INI not found at {}", inputs.kid_ini.display());
    }
    if !inputs.scenario_ini_dir.is_dir() {
        bail!(
            "scenario INI directory not found at {}",
            inputs.scenario_ini_dir.display()
        );
    }

    let data = inputs.output.join("Data");
    let plugins = data.join("SKSE").join("Plugins");
    std::fs::create_dir_all(&plugins).with_context(|| format!("creating {}", plugins.display()))?;

    // DLLs go under SKSE/Plugins (standard SKSE plugin location).
    std::fs::copy(inputs.kid_dll, plugins.join("KID.dll"))?;
    std::fs::copy(inputs.kid_ini, plugins.join("KID.ini"))?;
    std::fs::copy(inputs.harness_dll, plugins.join("MoraGoldenHarness.dll"))?;

    // Scenario INIs go DIRECTLY under Data/ — KID's LookupConfigs.cpp
    // calls distribution::get_configs("Data\\", "_KID") which scans
    // the Data root for files matching *_KID*.ini. Placing them in
    // SKSE/Plugins was the M4 bug that caused empty captures.
    for entry in std::fs::read_dir(inputs.scenario_ini_dir)? {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let Some(name) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        let lower = name.to_ascii_lowercase();
        // Match the semantic of mora-kid::ini::discover_kid_ini_files:
        // filename contains "_kid" AND ends in ".ini". Same set KID itself
        // discovers at runtime.
        if !(lower.ends_with(".ini") && lower.contains("_kid")) {
            continue;
        }
        std::fs::copy(entry.path(), data.join(&name))?;
    }
    Ok(())
}
