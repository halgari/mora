//! TES4 file-header parser.
//!
//! The TES4 record is always first in a plugin. It carries:
//!   - flags (ESM master bit, ESL light-master bit)
//!   - HEDR subrecord (version, num_records, next_object_id)
//!   - MAST subrecords (master plugin filenames, in local-index order)
//!   - CNAM (author), SNAM (description) — ignored by Mora

use crate::reader::{ReadError, le_f32, le_u32, read_cstr};
use crate::record::{RECORD_FLAG_LIGHT_MASTER, RECORD_FLAG_MASTER, read_record};
use crate::signature::{CNAM, HEDR, MAST, SNAM, TES4};
use crate::subrecord::SubrecordIter;

#[derive(Debug, thiserror::Error)]
pub enum Tes4Error {
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("not a TES4 record: got signature {0}")]
    NotTes4(String),
    #[error("missing HEDR subrecord")]
    MissingHedr,
    #[error("bad HEDR size: expected 12, got {0}")]
    BadHedrSize(usize),
}

/// Parsed TES4 header.
#[derive(Debug)]
pub struct Tes4Header {
    /// Record flags — bit 0x01 = ESM, bit 0x200 = ESL.
    pub flags: u32,
    /// From HEDR: version (e.g. 1.7 for SSE).
    pub version: f32,
    /// From HEDR: number of records in the plugin.
    pub num_records: u32,
    /// From HEDR: next object ID for runtime-generated forms.
    pub next_object_id: u32,
    /// Master plugin filenames in local-index order.
    pub masters: Vec<String>,
    /// Author string (optional).
    pub author: Option<String>,
    /// Description string (optional).
    pub description: Option<String>,
}

impl Tes4Header {
    pub fn is_esm(&self) -> bool {
        self.flags & RECORD_FLAG_MASTER != 0
    }
    pub fn is_esl(&self) -> bool {
        self.flags & RECORD_FLAG_LIGHT_MASTER != 0
    }
}

/// Parse the TES4 header from the start of a plugin byte buffer.
pub fn parse_tes4(bytes: &[u8]) -> Result<Tes4Header, Tes4Error> {
    let (rec, _) = read_record(bytes, 0)?;
    if rec.signature != TES4 {
        return Err(Tes4Error::NotTes4(rec.signature.to_string()));
    }

    let mut version = 0.0f32;
    let mut num_records = 0u32;
    let mut next_object_id = 0u32;
    let mut masters = Vec::new();
    let mut author = None;
    let mut description = None;
    let mut hedr_seen = false;

    let iter = SubrecordIter::new(rec.body);
    for sub in iter {
        let sub = sub?;
        match sub.signature {
            s if s == HEDR => {
                if sub.data.len() != 12 {
                    return Err(Tes4Error::BadHedrSize(sub.data.len()));
                }
                let (v, o) = le_f32(sub.data, 0)?;
                let (n, o) = le_u32(sub.data, o)?;
                let (nxt, _) = le_u32(sub.data, o)?;
                version = v;
                num_records = n;
                next_object_id = nxt;
                hedr_seen = true;
            }
            s if s == MAST => {
                let (name, _) = read_cstr(sub.data, 0, sub.data.len())?;
                masters.push(name.to_string());
            }
            s if s == CNAM => {
                let (name, _) = read_cstr(sub.data, 0, sub.data.len())?;
                author = Some(name.to_string());
            }
            s if s == SNAM => {
                let (name, _) = read_cstr(sub.data, 0, sub.data.len())?;
                description = Some(name.to_string());
            }
            _ => {} // ONAM, INTV, INCC, DATA — ignored for M2
        }
    }

    if !hedr_seen {
        return Err(Tes4Error::MissingHedr);
    }

    Ok(Tes4Header {
        flags: rec.flags,
        version,
        num_records,
        next_object_id,
        masters,
        author,
        description,
    })
}

#[cfg(test)]
mod tests {
    // Tests use the shared fixtures module; see Task 21's
    // tests/fixtures.rs for TES4 builder helpers.
}

#[cfg(test)]
mod smoke_tests {
    use super::*;

    fn build_minimal_tes4(masters: &[&str]) -> Vec<u8> {
        // Build subrecords
        let mut subs = Vec::new();
        // HEDR: version=1.7, num_records=0, next_object_id=0x800
        subs.extend_from_slice(b"HEDR");
        subs.extend_from_slice(&12u16.to_le_bytes());
        subs.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
        subs.extend_from_slice(&0u32.to_le_bytes());
        subs.extend_from_slice(&0x800u32.to_le_bytes());
        // Masters
        for m in masters {
            let name_bytes = m.as_bytes();
            let size = (name_bytes.len() + 1) as u16; // +1 for NUL
            subs.extend_from_slice(b"MAST");
            subs.extend_from_slice(&size.to_le_bytes());
            subs.extend_from_slice(name_bytes);
            subs.push(0); // NUL
            // DATA placeholder (8 bytes)
            subs.extend_from_slice(b"DATA");
            subs.extend_from_slice(&8u16.to_le_bytes());
            subs.extend_from_slice(&0u64.to_le_bytes());
        }

        // Build record header
        let mut buf = Vec::new();
        buf.extend_from_slice(b"TES4");
        buf.extend_from_slice(&(subs.len() as u32).to_le_bytes()); // data_size
        buf.extend_from_slice(&0x01u32.to_le_bytes()); // ESM flag
        buf.extend_from_slice(&0u32.to_le_bytes()); // form_id
        buf.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        buf.extend_from_slice(&44u16.to_le_bytes()); // version
        buf.extend_from_slice(&0u16.to_le_bytes()); // unknown
        buf.extend_from_slice(&subs);
        buf
    }

    #[test]
    fn parses_tes4_with_two_masters() {
        let buf = build_minimal_tes4(&["Skyrim.esm", "Update.esm"]);
        let h = parse_tes4(&buf).unwrap();
        assert!(h.is_esm());
        assert!(!h.is_esl());
        assert_eq!(h.version, 1.7);
        assert_eq!(h.masters, vec!["Skyrim.esm", "Update.esm"]);
    }

    #[test]
    fn parses_tes4_with_no_masters() {
        let buf = build_minimal_tes4(&[]);
        let h = parse_tes4(&buf).unwrap();
        assert!(h.masters.is_empty());
    }
}
