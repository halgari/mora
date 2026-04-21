//! End-to-end integration tests for mora-esp.
//!
//! Builds synthetic plugins via the `fixtures` module, writes to
//! tmp files, opens through EspPlugin / EspWorld, and asserts.

mod fixtures;

use std::io::Write;
use std::path::PathBuf;

use fixtures::*;
use mora_esp::records::{armor, weapon};
use mora_esp::signature::{ARMO, WEAP};
use mora_esp::{EspPlugin, EspWorld};

fn write_tmp(name: &str, bytes: &[u8]) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-esp-it-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    let mut f = std::fs::File::create(&path).unwrap();
    f.write_all(bytes).unwrap();
    path
}

#[test]
fn parses_single_plugin_with_weapon() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_2EB7)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("IronSword")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0001_E718]))),
            ),
        )
        .bytes();

    let path = write_tmp("TestPlugin.esm", &plugin_bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    assert!(plugin.is_esm());
    assert_eq!(plugin.filename, "TestPlugin.esm");

    // Manually walk the body to find the WEAP group and iterate.
    // (Full EspWorld end-to-end is tested below.)
}

#[test]
fn esp_world_iterates_weapons_across_plugins() {
    let bytes_a = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_AAAA)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("SwordA")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0001_E718]))),
            ),
        )
        .bytes();
    let bytes_b = PluginBuilder::new()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_BBBB)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("SwordB")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0002_E718]))),
            ),
        )
        .bytes();

    let dir = std::env::temp_dir().join(format!("mora-esp-world-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path_a = dir.join("A.esm");
    let path_b = dir.join("B.esp");
    std::fs::write(&path_a, &bytes_a).unwrap();
    std::fs::write(&path_b, &bytes_b).unwrap();

    let plugins_txt = dir.join("plugins.txt");
    std::fs::write(&plugins_txt, "*A.esm\n*B.esp\n").unwrap();

    let world = EspWorld::open(&dir, &plugins_txt).unwrap();
    assert!(
        world.plugins.len() >= 2,
        "expected A.esm + B.esp; got {} plugins",
        world.plugins.len()
    );

    let weapons: Vec<_> = world.records(WEAP).collect();
    assert!(weapons.iter().any(|w| w.record.form_id == 0x0001_AAAA));
    assert!(weapons.iter().any(|w| w.record.form_id == 0x0001_BBBB));
}

#[test]
fn weapon_parse_extracts_editor_id_and_keywords() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"WEAP").add(
                RecordBuilder::new(b"WEAP", 0x0001_2EB7)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("IronSword")))
                    .add(SubrecordBuilder::new(
                        b"KWDA",
                        kwda_payload(&[0x0001_E718, 0x0002_1234]),
                    )),
            ),
        )
        .bytes();

    let path = write_tmp("IronPlugin.esm", &plugin_bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-iron.txt");
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    for w in world.records(WEAP) {
        let parsed = weapon::parse(&w.record).unwrap();
        assert_eq!(parsed.editor_id.as_deref(), Some("IronSword"));
        assert_eq!(parsed.keywords, vec![0x0001_E718, 0x0002_1234]);
    }
}

#[test]
fn armor_parse_smoke() {
    let plugin_bytes = PluginBuilder::new()
        .esm()
        .add_group(
            GroupBuilder::new(b"ARMO").add(
                RecordBuilder::new(b"ARMO", 0x0001_CCCC)
                    .add(SubrecordBuilder::new(b"EDID", edid_payload("IronHelmet")))
                    .add(SubrecordBuilder::new(b"KWDA", kwda_payload(&[0x0001_E719]))),
            ),
        )
        .bytes();

    let path = write_tmp("ArmorPlugin.esm", &plugin_bytes);
    let _plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path.parent().unwrap().join("plugins-armor.txt");
    std::fs::write(
        &plugins_txt,
        format!(
            "*{}\n",
            path.file_name().unwrap().to_str().unwrap()
        ),
    )
    .unwrap();
    let world = EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap();

    for a in world.records(ARMO) {
        let parsed = armor::parse(&a.record).unwrap();
        assert_eq!(parsed.editor_id.as_deref(), Some("IronHelmet"));
        assert_eq!(parsed.keywords, vec![0x0001_E719]);
    }
}
