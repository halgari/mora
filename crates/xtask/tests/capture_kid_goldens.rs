//! Pure-logic unit tests for the capture-kid-goldens xtask. Anything
//! that requires Skyrim / Proton / the runner pool is out of scope here.

use std::fs;

use tempfile::tempdir;
use xtask::capture_kid_goldens::staging::{AssembleInputs, assemble_mod_dir};

#[test]
fn assemble_mod_dir_copies_all_fixtures_into_plugins_dir() {
    let tmp = tempdir().unwrap();
    let src = tmp.path().join("src");
    let scenario_ini_dir = src.join("scenario");
    let harness = src.join("harness");
    fs::create_dir_all(&scenario_ini_dir).unwrap();
    fs::create_dir_all(&harness).unwrap();
    fs::write(scenario_ini_dir.join("example_KID.ini"), b"; sample rule\n").unwrap();
    fs::write(
        scenario_ini_dir.join("notes.md"),
        b"should be skipped (not an ini)",
    )
    .unwrap();
    fs::write(harness.join("MoraGoldenHarness.dll"), b"\x4D\x5A fake PE").unwrap();

    let kid_dll = src.join("KID.dll");
    let kid_ini = src.join("KID.ini");
    fs::write(&kid_dll, b"\x4D\x5A fake KID PE").unwrap();
    fs::write(&kid_ini, b"; KID defaults\n").unwrap();

    let out = tmp.path().join("stage");
    assemble_mod_dir(&AssembleInputs {
        output: &out,
        kid_dll: &kid_dll,
        kid_ini: &kid_ini,
        scenario_ini_dir: &scenario_ini_dir,
        harness_dll: &harness.join("MoraGoldenHarness.dll"),
    })
    .unwrap();

    let plugins = out.join("Data").join("SKSE").join("Plugins");
    assert!(plugins.join("KID.dll").is_file());
    assert!(plugins.join("KID.ini").is_file());
    assert!(plugins.join("MoraGoldenHarness.dll").is_file());
    assert!(plugins.join("example_KID.ini").is_file());
    // README / non-INI fixtures must not be copied.
    assert!(!plugins.join("notes.md").exists());
}

#[test]
fn assemble_mod_dir_errors_on_missing_harness() {
    let tmp = tempdir().unwrap();
    let src = tmp.path().join("src");
    let scenario_ini_dir = src.join("scenario");
    fs::create_dir_all(&scenario_ini_dir).unwrap();
    fs::write(scenario_ini_dir.join("x_KID.ini"), b"").unwrap();
    fs::write(src.join("KID.dll"), b"").unwrap();
    fs::write(src.join("KID.ini"), b"").unwrap();

    let out = tmp.path().join("stage");
    let err = assemble_mod_dir(&AssembleInputs {
        output: &out,
        kid_dll: &src.join("KID.dll"),
        kid_ini: &src.join("KID.ini"),
        scenario_ini_dir: &scenario_ini_dir,
        harness_dll: &src.join("MoraGoldenHarness.dll"), // does not exist
    })
    .unwrap_err();
    assert!(
        err.to_string().contains("MoraGoldenHarness.dll"),
        "expected error to mention the missing harness file; got: {err}"
    );
}
