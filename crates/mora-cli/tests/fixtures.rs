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
        let dir = std::env::temp_dir().join(format!("mora-cli-it-{label}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();

        for (name, bytes) in plugins {
            let path = dir.join(name);
            std::fs::File::create(&path)
                .unwrap()
                .write_all(bytes)
                .unwrap();
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
    keywords: &[(&str, u32)],          // (editor_id, form_id)
    weapons: &[(u32, &str, Vec<u32>)], // (form_id, editor_id, keyword_form_ids)
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
