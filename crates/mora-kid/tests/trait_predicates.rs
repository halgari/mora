//! End-to-end tests for Plan 8b trait predicates.
//!
//! Builds synthetic plugins containing WEAP/ARMO records with the
//! new subrecord fields (DATA, DNAM, BOD2, EITM, etc.), runs
//! KidDistributor with rules using trait predicates, and verifies
//! the correct items are (or aren't) patched.

use std::io::Write;

use mora_core::{DeterministicChance, Distributor, FormId, PatchSink};
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_ini_content;

fn write_tmp(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-8b-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path)
        .unwrap()
        .write_all(bytes)
        .unwrap();
    path
}

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

/// Minimal TES4 header for an ESM.
fn tes4_esm_bytes() -> Vec<u8> {
    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes()); // ESM flag
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);
    out
}

/// Build a WEAP record with configurable DATA + DNAM + optional EITM.
fn weap_record(
    form_id: u32,
    edid: &str,
    anim: u8,
    damage: u16,
    weight: f32,
    enchantment: Option<u32>,
) -> Vec<u8> {
    let mut body = sub(b"EDID", &nul(edid));
    body.extend_from_slice(&sub(b"KWDA", &[])); // empty keyword list
    let mut data = Vec::new();
    data.extend_from_slice(&0u32.to_le_bytes()); // value
    data.extend_from_slice(&weight.to_le_bytes());
    data.extend_from_slice(&damage.to_le_bytes());
    body.extend_from_slice(&sub(b"DATA", &data));
    let mut dnam = vec![anim, 0, 0, 0];
    dnam.extend_from_slice(&1.0f32.to_le_bytes()); // speed
    dnam.extend_from_slice(&1.0f32.to_le_bytes()); // reach
    dnam.extend_from_slice(&[0u8; 88]); // rest of the 100-byte DNAM
    body.extend_from_slice(&sub(b"DNAM", &dnam));
    if let Some(ench) = enchantment {
        body.extend_from_slice(&sub(b"EITM", &ench.to_le_bytes()));
    }
    rec(b"WEAP", form_id, &body)
}

/// Build an ARMO record with configurable BOD2 + DATA + DNAM + optional EITM.
fn armo_record(
    form_id: u32,
    edid: &str,
    armor_type: u32,
    slots_bitmask: u32,
    weight: f32,
    ar: i32,
    enchantment: Option<u32>,
) -> Vec<u8> {
    let mut body = sub(b"EDID", &nul(edid));
    body.extend_from_slice(&sub(b"KWDA", &[]));
    let mut bod2 = Vec::new();
    bod2.extend_from_slice(&slots_bitmask.to_le_bytes());
    bod2.extend_from_slice(&armor_type.to_le_bytes());
    body.extend_from_slice(&sub(b"BOD2", &bod2));
    let mut data = Vec::new();
    data.extend_from_slice(&0i32.to_le_bytes()); // value
    data.extend_from_slice(&weight.to_le_bytes());
    body.extend_from_slice(&sub(b"DATA", &data));
    let mut dnam = Vec::new();
    dnam.extend_from_slice(&(ar as u32).to_le_bytes());
    body.extend_from_slice(&sub(b"DNAM", &dnam));
    if let Some(ench) = enchantment {
        body.extend_from_slice(&sub(b"EITM", &ench.to_le_bytes()));
    }
    rec(b"ARMO", form_id, &body)
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
fn weapon_animation_type_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut weapons = Vec::new();
    // Sword (anim=1), Bow (anim=7)
    weapons.extend_from_slice(&weap_record(0x0001_1100, "Sword", 1, 10, 5.0, None));
    weapons.extend_from_slice(&weap_record(0x0001_1101, "Bow", 7, 8, 3.0, None));
    bytes.extend_from_slice(&group(b"WEAP", &weapons));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_1200, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("animtype", bytes);
    // Rule targets only OneHandSword (anim_type == 1).
    let file = run(&world, "Target = Weapon||OneHandSword\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_1100));
}

#[test]
fn weapon_damage_range_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut weapons = Vec::new();
    weapons.extend_from_slice(&weap_record(0x0001_1200, "Low", 1, 3, 5.0, None));
    weapons.extend_from_slice(&weap_record(0x0001_1201, "Mid", 1, 10, 5.0, None));
    weapons.extend_from_slice(&weap_record(0x0001_1202, "High", 1, 99, 5.0, None));
    bytes.extend_from_slice(&group(b"WEAP", &weapons));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_1300, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("dmgrange", bytes);
    // Damage 5..=20: only "Mid" (damage=10) passes.
    let file = run(&world, "Target = Weapon||D(5 20)\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_1201));
}

#[test]
fn weapon_enchanted_predicate() {
    let mut bytes = tes4_esm_bytes();
    // Weapon A is enchanted (EITM = 0x0001_9999), Weapon B is not.
    // 0x0001_9999: top byte 0x00 == masters.len() (0 masters) → self-reference → resolves OK.
    let mut weapons = Vec::new();
    weapons.extend_from_slice(&weap_record(
        0x0001_1400,
        "Ench",
        1,
        10,
        5.0,
        Some(0x0001_9999),
    ));
    weapons.extend_from_slice(&weap_record(0x0001_1401, "Plain", 1, 10, 5.0, None));
    bytes.extend_from_slice(&group(b"WEAP", &weapons));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_1500, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("enchpred", bytes);
    // -E means NOT enchanted.
    let file = run(&world, "Target = Weapon||-E\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_1401));
}

#[test]
fn armor_type_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut armors = Vec::new();
    // Light (type=0), Heavy (type=1), Clothing (type=2)
    armors.extend_from_slice(&armo_record(
        0x0001_2100,
        "LightBoots",
        0,
        0x80,
        1.0,
        800,
        None,
    ));
    armors.extend_from_slice(&armo_record(
        0x0001_2101,
        "HeavyBoots",
        1,
        0x80,
        2.0,
        1200,
        None,
    ));
    armors.extend_from_slice(&armo_record(
        0x0001_2102,
        "ClothBoots",
        2,
        0x80,
        0.5,
        0,
        None,
    ));
    bytes.extend_from_slice(&group(b"ARMO", &armors));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_2200, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("armortype", bytes);
    let file = run(&world, "Target = Armor||HEAVY\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2101));
}

#[test]
fn armor_body_slot_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut armors = Vec::new();
    // Slot 32 (body) = bit 2 = 0x4
    armors.extend_from_slice(&armo_record(0x0001_2300, "Body", 1, 0x4, 5.0, 3000, None));
    // Slot 37 (feet) = bit 7 = 0x80
    armors.extend_from_slice(&armo_record(0x0001_2301, "Feet", 1, 0x80, 2.0, 1000, None));
    bytes.extend_from_slice(&group(b"ARMO", &armors));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_2400, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("bodyslot", bytes);
    let file = run(&world, "Target = Armor||32\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2300));
}

#[test]
fn armor_ar_range_filter() {
    let mut bytes = tes4_esm_bytes();
    let mut armors = Vec::new();
    // ar is stored × 100 on disk. Display values 5.0, 20.0, 50.0 → 500, 2000, 5000.
    armors.extend_from_slice(&armo_record(0x0001_2500, "AR5", 1, 0x4, 5.0, 500, None));
    armors.extend_from_slice(&armo_record(0x0001_2501, "AR20", 1, 0x4, 5.0, 2000, None));
    armors.extend_from_slice(&armo_record(0x0001_2502, "AR50", 1, 0x4, 5.0, 5000, None));
    bytes.extend_from_slice(&group(b"ARMO", &armors));
    let mut keywords = Vec::new();
    keywords.extend_from_slice(&rec(b"KYWD", 0x0001_2600, &sub(b"EDID", &nul("Target"))));
    bytes.extend_from_slice(&group(b"KYWD", &keywords));

    let world = open_world("arrange", bytes);
    // AR(10 30): only AR20 passes (display value).
    let file = run(&world, "Target = Armor||AR(10 30)\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2501));
}
