//! Integration tests for Plan 8a: activated ALL + ANY + ExclusiveGroup.

use std::io::Write;

use mora_core::{DeterministicChance, Distributor, FormId, PatchSink};
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_ini_content;

fn write_tmp(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-plan8a-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path)
        .unwrap()
        .write_all(bytes)
        .unwrap();
    path
}

// Lifted from Plan 6's distribute.rs — minimal plugin builder.
fn build_plugin(
    keywords: &[(&str, u32)],
    weapons: &[(u32, &str, Vec<u32>)],
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
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(&form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
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
    fn nul(s: &str) -> Vec<u8> {
        let mut v = s.as_bytes().to_vec();
        v.push(0);
        v
    }
    fn kwda(ids: &[u32]) -> Vec<u8> {
        let mut v = Vec::with_capacity(ids.len() * 4);
        for &id in ids {
            v.extend_from_slice(&id.to_le_bytes());
        }
        v
    }

    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);

    if !keywords.is_empty() {
        let mut contents = Vec::new();
        for (edid, form_id) in keywords {
            let body = sub(b"EDID", &nul(edid));
            contents.extend_from_slice(&rec(b"KYWD", *form_id, &body));
        }
        out.extend_from_slice(&group(b"KYWD", &contents));
    }
    if !weapons.is_empty() {
        let mut contents = Vec::new();
        for (form_id, edid, kwids) in weapons {
            let mut body = sub(b"EDID", &nul(edid));
            body.extend_from_slice(&sub(b"KWDA", &kwda(kwids)));
            contents.extend_from_slice(&rec(b"WEAP", *form_id, &body));
        }
        out.extend_from_slice(&group(b"WEAP", &contents));
    }
    if !armors.is_empty() {
        let mut contents = Vec::new();
        for (form_id, edid, kwids) in armors {
            let mut body = sub(b"EDID", &nul(edid));
            body.extend_from_slice(&sub(b"KWDA", &kwda(kwids)));
            contents.extend_from_slice(&rec(b"ARMO", *form_id, &body));
        }
        out.extend_from_slice(&group(b"ARMO", &contents));
    }

    out
}

fn open_world(suffix: &str, bytes: Vec<u8>) -> EspWorld {
    let path = write_tmp(&format!("{suffix}.esm"), &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join(format!("plugins-{suffix}.txt"));
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap()
}

fn run(world: &EspWorld, ini: &str) -> mora_core::PatchFile {
    let parsed = parse_ini_content(ini, "test.ini");
    let dist = KidDistributor::new(parsed.rules).with_exclusive_groups(parsed.exclusive_groups);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(world, &chance, &mut sink).unwrap();
    sink.finalize()
}

#[test]
fn all_filter_requires_every_ref_to_match() {
    // Plugin: 2 weapons, one with KW_A (and nothing else), one with KW_A + KW_B.
    let bytes = build_plugin(
        &[
            ("KW_A", 0x0001_1000),
            ("KW_B", 0x0001_1001),
            ("Target", 0x0001_1002),
        ],
        &[
            (0x0001_2000, "OnlyA", vec![0x0001_1000]),
            (0x0001_2001, "AandB", vec![0x0001_1000, 0x0001_1001]),
        ],
        &[],
    );
    let world = open_world("all", bytes);
    // Rule: add Target keyword to weapons with KW_A AND KW_B
    let file = run(&world, "Target = Weapon|KW_A+KW_B\n");
    // Only AandB should be targeted.
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2001));
}

#[test]
fn any_filter_substring_matches_editor_id() {
    let bytes = build_plugin(
        &[("Target", 0x0001_1100)],
        &[
            (0x0001_2100, "IronSword", vec![]),
            (0x0001_2101, "SteelAxe", vec![]),
        ],
        &[],
    );
    let world = open_world("anyed", bytes);
    let file = run(&world, "Target = Weapon|*Iron\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2100));
}

#[test]
fn any_filter_substring_matches_keyword_editor_id() {
    let bytes = build_plugin(
        &[("WeapMaterialIron", 0x0001_1200), ("Target", 0x0001_1201)],
        &[
            (0x0001_2200, "Weapon1", vec![0x0001_1200]),
            (0x0001_2201, "Weapon2", vec![]),
        ],
        &[],
    );
    let world = open_world("anykw", bytes);
    // *material substring should match WeapMaterialIron keyword edid on Weapon1
    let file = run(&world, "Target = Weapon|*material\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2200));
}

#[test]
fn exclusive_group_prevents_second_application() {
    let bytes = build_plugin(
        &[("KwA", 0x0001_1300), ("KwB", 0x0001_1301)],
        &[(0x0001_2300, "Sword", vec![])],
        &[],
    );
    let world = open_world("excl", bytes);
    // Two rules, both distributing to the same weapon. ExclusiveGroup
    // puts both keywords in one group → only the first should apply.
    let ini = "ExclusiveGroup = Mats|KwA,KwB\nKwA = Weapon\nKwB = Weapon\n";
    let file = run(&world, ini);
    assert_eq!(
        file.patches.len(),
        1,
        "exclusive group should limit to 1 patch, got {:?}",
        file.patches
    );
    // Either KwA or KwB wins — both are valid (iteration order in
    // the hash map isn't strictly defined). Just assert the set.
    let kw = match file.patches[0] {
        mora_core::Patch::AddKeyword { keyword, .. } => keyword,
    };
    assert!(
        kw == FormId(0x0001_1300) || kw == FormId(0x0001_1301),
        "unexpected winning keyword {kw:?}"
    );
}

#[test]
fn exclusive_group_independent_per_form() {
    // Two weapons, each gets its own independent "first keyword wins" treatment.
    let bytes = build_plugin(
        &[("KwA", 0x0001_1400), ("KwB", 0x0001_1401)],
        &[
            (0x0001_2400, "Sword1", vec![]),
            (0x0001_2401, "Sword2", vec![]),
        ],
        &[],
    );
    let world = open_world("exclmulti", bytes);
    let ini = "ExclusiveGroup = Mats|KwA,KwB\nKwA = Weapon\nKwB = Weapon\n";
    let file = run(&world, ini);
    // Each weapon gets exactly 1 keyword.
    assert_eq!(file.patches.len(), 2);
}
