//! End-to-end test: KidDistributor against a synthetic EspWorld.

use std::io::Write;

use mora_core::{DeterministicChance, Distributor, FormId, Patch, PatchSink};
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_ini_content;

fn write_tmp(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-dist-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path)
        .unwrap()
        .write_all(bytes)
        .unwrap();
    path
}

// Helper to build a minimal synthetic plugin with one KYWD, one WEAP, one ARMO.
fn build_plugin_bytes() -> Vec<u8> {
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
        v.extend_from_slice(&44u16.to_le_bytes()); // version
        v.extend_from_slice(&0u16.to_le_bytes()); // unknown
        v.extend_from_slice(body);
        v
    }
    fn group(label: &[u8; 4], contents: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&((24 + contents.len()) as u32).to_le_bytes());
        v.extend_from_slice(label);
        v.extend_from_slice(&0u32.to_le_bytes()); // group_type=0
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

    // TES4
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    // TES4 record
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes()); // ESM flag
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);

    // KYWD group: one keyword "WeapMaterialIron" @ FormID 0x0001_E718
    let kwbody = sub(b"EDID", &nul_cstr("WeapMaterialIron"));
    let kwrec = rec(b"KYWD", 0x0001_E718, &kwbody);
    out.extend_from_slice(&group(b"KYWD", &kwrec));

    // WEAP group: one weapon "IronSword" @ 0x0001_2EB7 with no keywords
    let mut wbody = sub(b"EDID", &nul_cstr("IronSword"));
    wbody.extend_from_slice(&sub(b"KWDA", &[])); // empty keyword list
    let wrec = rec(b"WEAP", 0x0001_2EB7, &wbody);
    out.extend_from_slice(&group(b"WEAP", &wrec));

    // ARMO group: one armor "IronHelmet" @ 0x0001_CCCC with no keywords
    let mut abody = sub(b"EDID", &nul_cstr("IronHelmet"));
    abody.extend_from_slice(&sub(b"KWDA", &[])); // empty keyword list
    let arec = rec(b"ARMO", 0x0001_CCCC, &abody);
    out.extend_from_slice(&group(b"ARMO", &arec));

    out
}

fn open_world_named(suffix: &str) -> (EspWorld, std::path::PathBuf) {
    let bytes = build_plugin_bytes();
    let plugin_name = format!("KidDistPlugin-{suffix}.esm");
    let path = write_tmp(&plugin_name, &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let data_dir = path.parent().unwrap();
    let plugins_txt = data_dir.join(format!("plugins-kid-{suffix}.txt"));
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(data_dir, &plugins_txt).unwrap();
    (world, data_dir.to_path_buf())
}

#[test]
fn distributes_keyword_to_weapon() {
    let (world, _) = open_world_named("weap");

    let rules = parse_ini_content("WeapMaterialIron = Weapon\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    let stats = dist.lower(&world, &chance, &mut sink).unwrap();

    assert_eq!(stats.patches_emitted, 1);
    let file = sink.finalize();
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
fn distributes_keyword_to_armor() {
    let (world, _) = open_world_named("armo");
    let rules = parse_ini_content("WeapMaterialIron = Armor\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    let stats = dist.lower(&world, &chance, &mut sink).unwrap();

    assert_eq!(stats.patches_emitted, 1);
    let file = sink.finalize();
    assert!(matches!(
        file.patches[0],
        Patch::AddKeyword {
            target: FormId(0x0001_CCCC),
            keyword: FormId(0x0001_E718)
        }
    ));
}

#[test]
fn unresolved_keyword_skips_rule() {
    let (world, _) = open_world_named("unresolved");
    let rules = parse_ini_content("NonExistentKeyword = Weapon\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(&world, &chance, &mut sink).unwrap();
    assert_eq!(sink.len(), 0);
}

#[test]
fn unsupported_record_type_skips_rule() {
    let (world, _) = open_world_named("unsupported");
    let rules = parse_ini_content("WeapMaterialIron = Potion\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(&world, &chance, &mut sink).unwrap();
    assert_eq!(sink.len(), 0);
}

#[test]
fn chance_zero_never_emits() {
    let (world, _) = open_world_named("chance0");
    let rules = parse_ini_content("WeapMaterialIron = Weapon|||0\n", "test.ini");
    let dist = KidDistributor::new(rules);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(&world, &chance, &mut sink).unwrap();
    assert_eq!(sink.len(), 0);
}
