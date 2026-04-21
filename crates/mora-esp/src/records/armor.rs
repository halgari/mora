//! `ARMO` — armor record accessor.
//!
//! M2 exposes the subset mora-kid needs:
//! - `editor_id` (EDID subrecord)
//! - `keywords` (KWDA subrecord — local FormIDs)

use crate::compression::{decompress, DecompressError};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::{EDID, KWDA};
use crate::subrecord::SubrecordIter;
use crate::subrecords::{edid, kwda};

#[derive(Debug, thiserror::Error)]
pub enum ArmorError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

/// Parsed ARMO record.
#[derive(Debug, Default)]
pub struct ArmorRecord {
    pub editor_id: Option<String>,
    pub keywords: Vec<u32>,
}

pub fn parse(record: &Record<'_>) -> Result<ArmorRecord, ArmorError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut a = ArmorRecord::default();
    let mut iter = SubrecordIter::new(body);
    while let Some(sub) = iter.next() {
        let sub = sub?;
        match sub.signature {
            s if s == EDID => a.editor_id = Some(edid::parse(sub.data)?),
            s if s == KWDA => a.keywords = kwda::parse(sub.data)?,
            _ => {}
        }
    }
    Ok(a)
}

#[cfg(test)]
mod tests {
    // Full ARMO parsing tests land in tests/esp_format.rs (Task 21).
}
