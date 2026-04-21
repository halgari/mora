//! End-to-end integration tests for `mora compile`.
//!
//! Each test builds a synthetic Skyrim Data dir, invokes the compiled
//! `mora` binary, and verifies the resulting patch file.

mod fixtures;

use std::process::Command;

use fixtures::*;
use mora_core::{FormId, Patch, PatchFile};

fn run_mora_compile(
    fixture: &Fixture,
    output: &std::path::Path,
    extra_args: &[&str],
) -> std::process::Output {
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
    let plugins_txt =
        std::env::temp_dir().join(format!("mora-bad-plugins-{}.txt", std::process::id()));
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
    assert!(
        !out.status.success(),
        "expected non-zero exit for bad data-dir"
    );
    let stderr = String::from_utf8_lossy(&out.stderr);
    assert!(
        stderr.contains("data-dir"),
        "stderr should mention data-dir: {stderr}"
    );

    let _ = std::fs::remove_file(&tmpfile);
    let _ = std::fs::remove_file(&plugins_txt);
}

#[test]
fn compile_empty_kid_ini_set_produces_empty_patches() {
    // No KID INIs at all → zero patches emitted, patch file still well-formed.
    let bytes = build_plugin(true, &[], &[(0x0001_2EB7, "IronSword", vec![])], &[]);
    let fixture = Fixture::new("empty-inis", &[("Test.esm", bytes)]);

    let output = fixture.data_dir.join("mora_patches.bin");
    let out = run_mora_compile(&fixture, &output, &[]);
    assert!(out.status.success());
    let file = PatchFile::from_bytes(&std::fs::read(&output).unwrap()).unwrap();
    assert!(file.patches.is_empty());
    assert_eq!(&file.magic, b"MORA");
}
