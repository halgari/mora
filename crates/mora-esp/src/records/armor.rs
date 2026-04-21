//! `ARMO` — armor record accessor.
//!
//! M3-complete fields (used by KID trait predicates):
//!   - editor_id       : EDID
//!   - keywords        : KWDA (resolved)
//!   - armor_type      : BOD2/BODT — LightArmor/HeavyArmor/Clothing
//!   - body_slots      : BOD2/BODT — list of slot numbers 30..=61
//!   - armor_rating    : DNAM (display scale)
//!   - value, weight   : DATA
//!   - enchantment     : EITM resolved FormId
//!   - template_armor  : TNAM (NOT CNAM!) resolved FormId

use mora_core::FormId;

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{BOD2, BODT, DATA, DNAM, EDID, EITM, KWDA, TNAM};
use crate::subrecord::SubrecordIter;
use crate::subrecords::biped_object::{ArmorType, BipedObject, parse_bod2, parse_bodt};
use crate::subrecords::{armor_data, armor_dnam, edid, form_id_ref, kwda};
use crate::world::EspWorld;

#[derive(Debug, thiserror::Error)]
pub enum ArmorError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

#[derive(Debug, Default)]
pub struct ArmorRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<FormId>,
    pub armor_type: Option<ArmorType>,
    pub body_slots: Vec<u8>,
    pub armor_rating: Option<f32>,
    pub value: Option<i32>,
    pub weight: Option<f32>,
    pub enchantment: Option<FormId>,
    pub template_armor: Option<FormId>,
}

pub fn parse(
    record: &Record<'_>,
    plugin_index: usize,
    world: &EspWorld,
) -> Result<ArmorRecord, ArmorError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut a = ArmorRecord::default();
    // Track biped object separately so BOD2 can override BODT if both present.
    let mut biped_from_bod2: Option<BipedObject> = None;
    let mut biped_from_bodt: Option<BipedObject> = None;

    for sub in SubrecordIter::new(body) {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => a.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => {
                let local_ids = kwda::parse(sub.data)?;
                a.keywords = local_ids
                    .into_iter()
                    .filter_map(|raw| world.resolve_in_plugin(plugin_index, raw))
                    .collect();
            }
            s if s == BOD2 => {
                if let Ok(b) = parse_bod2(sub.data) {
                    biped_from_bod2 = Some(b);
                }
            }
            s if s == BODT => {
                if let Ok(b) = parse_bodt(sub.data) {
                    biped_from_bodt = Some(b);
                }
            }
            s if s == DATA => {
                if let Ok(d) = armor_data::parse(sub.data) {
                    a.value = Some(d.value);
                    a.weight = Some(d.weight);
                }
            }
            s if s == DNAM => {
                if let Ok(d) = armor_dnam::parse(sub.data) {
                    a.armor_rating = Some(d.armor_rating);
                }
            }
            s if s == EITM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    a.enchantment = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            s if s == TNAM => {
                if let Ok(raw) = form_id_ref::parse(sub.data) {
                    a.template_armor = world.resolve_in_plugin(plugin_index, raw);
                }
            }
            _ => {}
        }
    }

    // Prefer BOD2 over BODT (BOD2 is the SSE primary format).
    let biped = biped_from_bod2.or(biped_from_bodt);
    if let Some(b) = biped {
        a.armor_type = b.armor_type;
        a.body_slots = b.slot_numbers();
    }

    Ok(a)
}

#[cfg(test)]
mod tests {
    // Full ARMO parsing tests land in tests/trait_predicates.rs (Plan 8b).
}
