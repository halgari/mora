//! `WEAP` — weapon record accessor.
//!
//! M3-complete fields (used by KID trait predicates):
//!   - editor_id       : EDID subrecord
//!   - keywords        : KWDA (resolved FormIds)
//!   - animation_type  : DNAM offset 0x00 (u8 enum)
//!   - speed, reach    : DNAM offsets 0x04, 0x08 (f32)
//!   - value, weight, damage : DATA subrecord
//!   - enchantment     : Some(FormId) if EITM present, else None
//!   - template_weapon : Some(FormId) if CNAM present, else None
//!
//! Keyword FormIDs are resolved at parse time against the active load
//! order; EITM + CNAM local FormIDs are also resolved via
//! `EspWorld::resolve_in_plugin`. Unresolvable refs silently become
//! None (matches KID).

use mora_core::FormId;

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{CNAM, DATA, DNAM, EDID, EITM, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::weapon_dnam::WeaponAnimType;
use crate::subrecords::{edid, form_id_ref, kwda, weapon_data, weapon_dnam};
use crate::world::EspWorld;

#[derive(Debug, thiserror::Error)]
pub enum WeaponError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

#[derive(Debug, Default)]
pub struct WeaponRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<FormId>,
    pub animation_type: Option<WeaponAnimType>,
    pub speed: Option<f32>,
    pub reach: Option<f32>,
    pub value: Option<u32>,
    pub weight: Option<f32>,
    pub damage: Option<u16>,
    pub enchantment: Option<FormId>,
    pub template_weapon: Option<FormId>,
}

pub fn parse(
    record: &Record<'_>,
    plugin_index: usize,
    world: &EspWorld,
) -> Result<WeaponRecord, WeaponError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut w = WeaponRecord::default();
    for sub in SubrecordIter::new(body) {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => w.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => {
                let local_ids = kwda::parse(sub.data)?;
                w.keywords = local_ids
                    .into_iter()
                    .filter_map(|raw| world.resolve_in_plugin(plugin_index, raw))
                    .collect();
            }
            s if s == DATA => {
                if let Ok(data) = weapon_data::parse(sub.data) {
                    w.value = Some(data.value);
                    w.weight = Some(data.weight);
                    w.damage = Some(data.damage);
                }
            }
            s if s == DNAM => {
                if let Ok(dnam) = weapon_dnam::parse(sub.data) {
                    w.animation_type = dnam.animation_type;
                    w.speed = Some(dnam.speed);
                    w.reach = Some(dnam.reach);
                }
            }
            s if s == EITM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    w.enchantment = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            s if s == CNAM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    w.template_weapon = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            _ => {}
        }
    }
    Ok(w)
}

#[cfg(test)]
mod tests {
    // Full WEAP parsing tests land in tests/esp_format.rs (Plan 5
    // extended here + in Plan 8b's trait_predicates tests).
}
