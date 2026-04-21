//! `WEAP` — weapon record accessor.
//!
//! M2 exposes the subset mora-kid needs:
//! - `editor_id` (EDID subrecord)
//! - `keywords` (KWDA subrecord — local FormIDs)
//!
//! Fields not yet exposed (added when consumers need them):
//! DNAM (damage, weight, value, reach, speed), NNAM (sound), etc.

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{EDID, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::{edid, kwda};

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
    pub keywords: Vec<u32>, // local FormIDs; caller remaps
}

/// Parse a WEAP record's body (handling compression).
pub fn parse(record: &Record<'_>) -> Result<WeaponRecord, WeaponError> {
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
            s if s == KWDA => w.keywords = kwda::parse(sub.data)?,
            _ => {} // other subrecords ignored at M2
        }
    }
    Ok(w)
}

#[cfg(test)]
mod tests {
    // Full WEAP parsing tests land in tests/esp_format.rs (Task 21).
}
