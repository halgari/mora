//! Integration tests for Reference resolution against an EspWorld.

use std::io::Write;

use mora_core::FormId;
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::Reference;

fn write_tmp_plugin(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-ref-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path).unwrap().write_all(bytes).unwrap();
    path
}

fn build_keyword_plugin() -> Vec<u8> {
    // Minimal plugin with one KYWD record.
    let mut v = Vec::new();
    // TES4 header (ESM flag)
    v.extend_from_slice(b"TES4");
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());
    v.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    v.extend_from_slice(&1u32.to_le_bytes()); // ESM flag
    v.extend_from_slice(&0u32.to_le_bytes()); // form_id
    v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
    v.extend_from_slice(&44u16.to_le_bytes()); // version
    v.extend_from_slice(&0u16.to_le_bytes()); // unknown
    v.extend_from_slice(&tes4_body);
    // KYWD group
    let mut kywd_rec = Vec::new();
    kywd_rec.extend_from_slice(b"KYWD");
    let mut kywd_body = Vec::new();
    kywd_body.extend_from_slice(b"EDID");
    kywd_body.extend_from_slice(&16u16.to_le_bytes()); // NUL-terminated "WeapMaterialIron" = 16 bytes + NUL
    kywd_body.extend_from_slice(b"WeapMaterialIron\0");
    // Above was 17 bytes total (16 ascii + 1 NUL), so fix size:
    let edid_payload_len = kywd_body.len() - (4 + 2); // minus sig+size
    // Rewrite size correctly: the "16u16" above was wrong — rewrite in place.
    let size_offset = 4;
    let correct_size = (edid_payload_len as u16).to_le_bytes();
    kywd_body[size_offset..size_offset + 2].copy_from_slice(&correct_size);
    kywd_rec.extend_from_slice(&(kywd_body.len() as u32).to_le_bytes()); // data_size
    kywd_rec.extend_from_slice(&0u32.to_le_bytes()); // flags
    kywd_rec.extend_from_slice(&0x0001_E718u32.to_le_bytes()); // form_id
    kywd_rec.extend_from_slice(&0u32.to_le_bytes()); // vc_info
    kywd_rec.extend_from_slice(&44u16.to_le_bytes()); // version
    kywd_rec.extend_from_slice(&0u16.to_le_bytes()); // unknown
    kywd_rec.extend_from_slice(&kywd_body);
    // Group wrapping
    let mut grp = Vec::new();
    grp.extend_from_slice(b"GRUP");
    grp.extend_from_slice(&((24 + kywd_rec.len()) as u32).to_le_bytes()); // group_size
    grp.extend_from_slice(b"KYWD"); // label
    grp.extend_from_slice(&0u32.to_le_bytes()); // group_type = 0
    grp.extend_from_slice(&0u16.to_le_bytes()); // timestamp
    grp.extend_from_slice(&0u16.to_le_bytes()); // vc_info
    grp.extend_from_slice(&0u32.to_le_bytes()); // unknown
    grp.extend_from_slice(&kywd_rec);
    v.extend_from_slice(&grp);
    v
}

#[test]
fn resolve_editor_id_against_keyword_plugin() {
    let bytes = build_keyword_plugin();
    let path = write_tmp_plugin("KwRefPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-ref.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    let r = Reference::parse("WeapMaterialIron");
    let resolved = r.resolve_form(&world);
    assert_eq!(resolved, Some(FormId(0x0001_E718)));
}

#[test]
fn resolve_form_id_with_plugin_against_world() {
    let bytes = build_keyword_plugin();
    let path = write_tmp_plugin("KwFormPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-form.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    let r = Reference::parse("0x1E718~KwFormPlugin.esm");
    let resolved = r.resolve_form(&world);
    // plugin slot depends on load order; KwFormPlugin.esm should be slot 0x00.
    // FormId = 0x00 << 24 | (0x1E718 & 0xFFFFFF) = 0x0001E718
    assert_eq!(resolved, Some(FormId(0x0001_E718)));
}

#[test]
fn unknown_editor_id_returns_none() {
    let bytes = build_keyword_plugin();
    let path = write_tmp_plugin("KwUnknownPlugin.esm", &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-unknown.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    let r = Reference::parse("NonExistent");
    assert!(r.resolve_form(&world).is_none());
}
