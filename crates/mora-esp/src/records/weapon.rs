//! `WEAP` — weapon record accessor.
//!
//! M2 exposes the subset mora-kid needs:
//! - `editor_id` (EDID subrecord)
//! - `keywords` (KWDA subrecord, resolved to runtime FormIDs)
//!
//! Fields not yet exposed (added when consumers need them):
//! DNAM (damage, weight, value, reach, speed), NNAM (sound), etc.
//!
//! Keyword FormIDs are **resolved** against the active load order at
//! parse time — the returned `WeaponRecord::keywords` is `Vec<FormId>`
//! where each FormID is already remapped via the plugin's master list.
//! Unresolvable keywords (referenced plugin not in load order) are
//! silently dropped, matching KID behavior.

use mora_core::FormId;

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{EDID, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::{edid, kwda};
use crate::world::EspWorld;

#[derive(Debug, thiserror::Error)]
pub enum WeaponError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

/// Parsed WEAP record.
#[derive(Debug, Default)]
pub struct WeaponRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<FormId>,
}

/// Parse a WEAP record's body, resolving all keyword FormIDs against
/// the active load order.
///
/// `plugin_index` is the index into `world.plugins` of the plugin this
/// record came from — used to look up the plugin's master list for
/// FormID remapping.
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
    let iter = SubrecordIter::new(body);
    for sub in iter {
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
            _ => {} // other subrecords ignored at M2
        }
    }
    Ok(w)
}

#[cfg(test)]
mod tests {
    // Full WEAP parsing tests land in tests/esp_format.rs (Task 21).
}
