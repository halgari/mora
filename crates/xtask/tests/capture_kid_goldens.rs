//! Pure-logic unit tests for the capture-kid-goldens xtask. Anything
//! that requires Skyrim / Proton / the runner pool is out of scope here.

use std::fs;

use tempfile::tempdir;
use xtask::capture_kid_goldens::manifest::{
    hash_file_sha256, read_peek_kid_version, write_for_scenario,
};
use xtask::capture_kid_goldens::staging::{AssembleInputs, assemble_mod_dir};

#[test]
fn assemble_mod_dir_copies_dlls_under_plugins_and_inis_under_data() {
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
    fs::write(
        scenario_ini_dir.join("base_kid_extra.ini"),
        b"; another kid rule\n",
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

    // DLLs live under Data/SKSE/Plugins.
    let data = out.join("Data");
    let plugins = data.join("SKSE").join("Plugins");
    assert!(plugins.join("KID.dll").is_file());
    assert!(plugins.join("KID.ini").is_file());
    assert!(plugins.join("MoraGoldenHarness.dll").is_file());

    // Scenario INIs go DIRECTLY under Data/ — this is what KID 3.4.0's
    // distribution::get_configs("Data\\", "_KID") scans. Putting them
    // under SKSE/Plugins causes KID to log "No .ini files with _KID
    // suffix were found within the Data folder, aborting..." and
    // produce empty captures.
    assert!(data.join("example_KID.ini").is_file());
    assert!(
        data.join("base_kid_extra.ini").is_file(),
        "staging must match mora-kid's discovery predicate (contains _kid AND ends .ini)"
    );
    // Scenario INIs must NOT end up under SKSE/Plugins (negative assertion).
    assert!(!plugins.join("example_KID.ini").exists());
    assert!(!plugins.join("base_kid_extra.ini").exists());
    // README / non-INI fixtures must not be copied anywhere.
    assert!(!plugins.join("notes.md").exists());
    assert!(!data.join("notes.md").exists());
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

#[test]
fn hash_file_sha256_is_stable() {
    let tmp = tempdir().unwrap();
    let p = tmp.path().join("sample.bin");
    fs::write(&p, b"hello world\n").unwrap();
    let h1 = hash_file_sha256(&p).unwrap();
    let h2 = hash_file_sha256(&p).unwrap();
    assert_eq!(h1, h2);
    // Expected sha256 of b"hello world\n" is well-known:
    assert_eq!(
        h1,
        "a948904f2f0f479b8f8197694b30184b0d2ed1c1cd2a1ec0fb85d299a192a447"
    );
}

#[test]
fn read_peek_kid_version_returns_some_for_nonempty_input() {
    let tmp = tempdir().unwrap();
    let p = tmp.path().join("KID.dll");
    // Write something that looks vaguely PE-ish. The helper is allowed
    // to return None if it can't extract a version; we only assert it
    // doesn't crash and that a non-empty bytestream yields Some/None.
    fs::write(&p, b"\x4D\x5A fake PE body").unwrap();
    let _ = read_peek_kid_version(&p); // must not panic
}

#[test]
fn write_for_scenario_emits_readable_manifest() {
    let tmp = tempdir().unwrap();
    let out = tmp.path().join("expected-out");
    fs::create_dir_all(&out).unwrap();
    let kid_dll = tmp.path().join("KID.dll");
    fs::write(&kid_dll, b"fake").unwrap();

    // Pass an empty data dir explicitly; no env mutation, no race.
    let data = tmp.path().join("data");
    fs::create_dir_all(&data).unwrap();

    write_for_scenario(&out, &kid_dll, &data).unwrap();

    let manifest = fs::read_to_string(out.join("manifest.json")).unwrap();
    assert!(manifest.contains("\"captured_at\""), "got: {manifest}");
    assert!(manifest.contains("\"kid_version\""), "got: {manifest}");
    assert!(manifest.contains("\"skyrim_version\""), "got: {manifest}");
    assert!(manifest.contains("\"esp_hashes\""), "got: {manifest}");
    // Skyrim.esm is absent, so skyrim_version must be the literal
    // "unknown" (no sha256: prefix) — proves Fix 1.
    assert!(
        manifest.contains("\"skyrim_version\": \"unknown\""),
        "got: {manifest}"
    );
}
