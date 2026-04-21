//! Load-order resolver — maps plugin filenames to runtime mod indices.
//!
//! SSE/AE mod-index layout:
//!   0x00                 Skyrim.esm (implicit)
//!   0x01                 Update.esm (implicit)
//!   0x02-0x04            Dawnguard / HearthFires / Dragonborn (if present)
//!   0x05-0xFD            User ESMs + ESPs in plugins.txt order
//!   0xFE + 12-bit slot   ESL pool (all ESL-flagged plugins)
//!   0xFF                 Reserved (runtime forms)

use std::collections::HashMap;

/// One entry in the resolved load order.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LoadSlot {
    /// Full mod index (0x00..=0xFD).
    Full(u8),
    /// Light-slot pool — 0x000..=0xFFF within the 0xFE high-byte.
    Light(u16),
}

impl LoadSlot {
    /// The high byte of a FormID from this slot.
    pub fn high_byte(&self) -> u8 {
        match self {
            LoadSlot::Full(b) => *b,
            LoadSlot::Light(_) => 0xFE,
        }
    }

    /// Compose a full FormID from this slot + a local 24-bit id.
    /// For light slots, only 12 bits of local_id are used (0x000-0xFFF).
    pub fn compose_form_id(&self, local_id: u32) -> u32 {
        match self {
            LoadSlot::Full(b) => ((*b as u32) << 24) | (local_id & 0x00FF_FFFF),
            LoadSlot::Light(slot) => {
                (0xFEu32 << 24) | ((*slot as u32) << 12) | (local_id & 0x0FFF)
            }
        }
    }
}

/// Resolved load order — maps plugin filename (case-insensitive) to its slot.
#[derive(Debug, Default)]
pub struct LoadOrder {
    /// Preserves original casing of filenames.
    pub plugin_names: Vec<String>,
    /// Lowercase → slot.
    pub slots: HashMap<String, LoadSlot>,
}

impl LoadOrder {
    pub fn lookup(&self, plugin_name: &str) -> Option<&LoadSlot> {
        self.slots.get(&plugin_name.to_lowercase())
    }
}

/// Bethesda implicit ESMs loaded before plugins.txt.
pub const IMPLICIT_PLUGINS: &[&str] = &[
    "Skyrim.esm",
    "Update.esm",
    "Dawnguard.esm",
    "HearthFires.esm",
    "Dragonborn.esm",
];

/// Build a load order from (implicit plugins + active user plugins).
///
/// `implicit_present`: which of `IMPLICIT_PLUGINS` actually exist in
/// the user's Data directory (caller's responsibility to check).
pub fn build(
    implicit_present: &[&str],
    active_user_plugins: &[&str],
    is_esl: &dyn Fn(&str) -> bool,
) -> LoadOrder {
    let mut order = LoadOrder::default();
    let mut next_full: u8 = 0;
    let mut next_light: u16 = 0;

    let assign = |name: &str, esl: bool, order: &mut LoadOrder, nf: &mut u8, nl: &mut u16| {
        let slot = if esl {
            let s = LoadSlot::Light(*nl);
            *nl += 1;
            s
        } else {
            let s = LoadSlot::Full(*nf);
            *nf = nf.checked_add(1).expect("full-slot overflow > 0xFE");
            s
        };
        order.plugin_names.push(name.to_string());
        order.slots.insert(name.to_lowercase(), slot);
    };

    for &name in implicit_present {
        assign(name, is_esl(name), &mut order, &mut next_full, &mut next_light);
    }
    for &name in active_user_plugins {
        // Skip if already present (user listed an implicit plugin
        // in their plugins.txt — some tools do).
        if order.lookup(name).is_some() {
            continue;
        }
        assign(name, is_esl(name), &mut order, &mut next_full, &mut next_light);
    }

    order
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn full_slot_layout() {
        let order = build(
            &["Skyrim.esm", "Update.esm"],
            &["UserMod.esp"],
            &|_| false,
        );
        assert_eq!(order.lookup("Skyrim.esm"), Some(&LoadSlot::Full(0x00)));
        assert_eq!(order.lookup("Update.esm"), Some(&LoadSlot::Full(0x01)));
        assert_eq!(order.lookup("UserMod.esp"), Some(&LoadSlot::Full(0x02)));
    }

    #[test]
    fn esl_goes_to_light_pool() {
        let order = build(
            &["Skyrim.esm"],
            &["LightMod.esl", "HeavyMod.esp"],
            &|n| n.ends_with(".esl"),
        );
        assert_eq!(order.lookup("Skyrim.esm"), Some(&LoadSlot::Full(0x00)));
        assert_eq!(order.lookup("LightMod.esl"), Some(&LoadSlot::Light(0x000)));
        // HeavyMod.esp gets the NEXT full slot, which is 0x01 (light
        // doesn't consume a full slot).
        assert_eq!(order.lookup("HeavyMod.esp"), Some(&LoadSlot::Full(0x01)));
    }

    #[test]
    fn case_insensitive_lookup() {
        let order = build(&["Skyrim.esm"], &[], &|_| false);
        assert!(order.lookup("SKYRIM.ESM").is_some());
        assert!(order.lookup("skyrim.esm").is_some());
    }

    #[test]
    fn form_id_composition_full() {
        let slot = LoadSlot::Full(0x02);
        assert_eq!(slot.compose_form_id(0x0001_2EB7), 0x02_01_2E_B7);
    }

    #[test]
    fn form_id_composition_light() {
        let slot = LoadSlot::Light(0x123);
        // 0xFE << 24 | 0x123 << 12 | local & 0xFFF
        // local 0x0ABC → 0xFE_12_3A_BC
        assert_eq!(slot.compose_form_id(0x0000_0ABC), 0xFE_12_3A_BC);
        // local's high bits > 12 are truncated: 0xFFF_ABC → 0xABC
        assert_eq!(slot.compose_form_id(0x00FF_FABC), 0xFE_12_3A_BC);
    }
}

/// Remap a plugin-local FormID (high byte = local mod index) into a
/// fully-resolved FormID using the plugin's master list + the live
/// load order.
///
/// `raw_form_id`: 32-bit FormID from a record or reference subrecord.
/// `masters`: plugin's master list (in local-index order).
/// `self_name`: the plugin's own filename (used when the local index
/// equals `masters.len()` — which means "this plugin's own forms").
/// `load_order`: the resolved runtime load order.
///
/// Returns `None` if the local index is out of range or the referenced
/// plugin is not in the load order.
pub fn remap_form_id(
    raw_form_id: u32,
    masters: &[String],
    self_name: &str,
    load_order: &LoadOrder,
) -> Option<u32> {
    let local_index = (raw_form_id >> 24) as usize;
    let local_id = raw_form_id & 0x00FF_FFFF;

    let referenced_name = if local_index < masters.len() {
        &masters[local_index]
    } else if local_index == masters.len() {
        self_name
    } else {
        return None;
    };

    let slot = load_order.lookup(referenced_name)?;
    Some(slot.compose_form_id(local_id))
}

#[cfg(test)]
mod remap_tests {
    use super::*;

    fn build_order() -> LoadOrder {
        build(
            &["Skyrim.esm", "Update.esm"],
            &["MyMod.esp", "MyLight.esl"],
            &|n| n.ends_with(".esl"),
        )
    }

    #[test]
    fn remap_via_master_zero() {
        // MyMod.esp references Skyrim.esm via local index 0.
        // raw 0x00_ABCDEF → Skyrim.esm (full slot 0x00), local 0xABCDEF
        //   → 0x00_ABCDEF
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];
        let out = remap_form_id(0x00_AB_CD_EF, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0x00_AB_CD_EF);
    }

    #[test]
    fn remap_via_master_one() {
        // Update.esm → slot 0x01, local 0xABCDEF → 0x01_ABCDEF
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];
        let out = remap_form_id(0x01_AB_CD_EF, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0x01_AB_CD_EF);
    }

    #[test]
    fn remap_self_reference() {
        // raw 0x02_XYZ with masters = [Skyrim, Update] → local index 2 = self = MyMod.esp
        // MyMod.esp is the first user plugin → full slot 0x02.
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string(), "Update.esm".to_string()];
        let out = remap_form_id(0x02_AB_CD_EF, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0x02_AB_CD_EF);
    }

    #[test]
    fn remap_to_esl() {
        // MyLight.esl is in the light pool at slot 0.
        let order = build_order();
        // If MyMod.esp adds MyLight.esl as a master (hypothetically),
        // then local index 2 = MyLight.esl.
        let masters = vec![
            "Skyrim.esm".to_string(),
            "Update.esm".to_string(),
            "MyLight.esl".to_string(),
        ];
        // raw 0x02_ABC with local index 2 (MyLight.esl).
        // ESL composes as FE | slot(0) << 12 | local & 0xFFF
        //   → 0xFE_00_0A_BC
        let out = remap_form_id(0x02_00_0A_BC, &masters, "MyMod.esp", &order).unwrap();
        assert_eq!(out, 0xFE_00_0A_BC);
    }

    #[test]
    fn remap_out_of_range_returns_none() {
        let order = build_order();
        let masters = vec!["Skyrim.esm".to_string()];
        // Local index 5, but only 1 master + self = valid indices are 0, 1
        assert!(remap_form_id(0x05_00_00_01, &masters, "MyMod.esp", &order).is_none());
    }
}
