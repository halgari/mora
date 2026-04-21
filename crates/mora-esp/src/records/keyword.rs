//! `KYWD` — keyword record accessor.
//!
//! A keyword record has only one subrecord of interest to Mora:
//! `EDID` (editor ID). Keywords typically do not carry KWDA themselves.

use crate::compression::{DecompressError, decompress};
use crate::reader::ReadError;
use crate::record::Record;
use crate::signature::EDID;
use crate::subrecord::SubrecordIter;
use crate::subrecords::edid;

#[derive(Debug, thiserror::Error)]
pub enum KeywordError {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("decompress: {0}")]
    Decompress(#[from] DecompressError),
}

/// Parsed KYWD record.
#[derive(Debug, Default)]
pub struct KeywordRecord {
    pub editor_id: Option<String>,
}

/// Parse a KYWD record.
pub fn parse(record: &Record<'_>) -> Result<KeywordRecord, KeywordError> {
    let body_owned: Option<Vec<u8>> = if record.is_compressed() {
        Some(decompress(record.body)?)
    } else {
        None
    };
    let body: &[u8] = body_owned.as_deref().unwrap_or(record.body);

    let mut k = KeywordRecord::default();
    let iter = SubrecordIter::new(body);
    for sub in iter {
        let sub = sub?;
        if sub.signature == EDID {
            k.editor_id = Some(edid::parse(sub.data)?);
        }
    }
    Ok(k)
}
