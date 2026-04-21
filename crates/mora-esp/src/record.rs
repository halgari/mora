//! Record header (24 bytes) parser.
//!
//! See `docs/src/mora-esp-reference.md`. Parses the header and
//! exposes the record body slice. Subrecord iteration is handled
//! in `subrecord.rs`; compression is `compression.rs`.

use crate::reader::{ReadError, le_u16, le_u32, read_signature};
use crate::signature::Signature;

pub const RECORD_HEADER_SIZE: usize = 24;

/// Bit mask for the "compressed" flag.
pub const RECORD_FLAG_COMPRESSED: u32 = 0x0004_0000;
/// Bit mask for the "deleted" flag.
pub const RECORD_FLAG_DELETED: u32 = 0x0000_0400;
/// Bit mask for the ESM master flag (TES4 only).
pub const RECORD_FLAG_MASTER: u32 = 0x0000_0001;
/// Bit mask for the "light master" (ESL) flag (TES4 only).
pub const RECORD_FLAG_LIGHT_MASTER: u32 = 0x0000_0200;

/// A parsed record header. Lifetime `'a` ties the body slice to the
/// source byte buffer (typically an mmap).
#[derive(Debug)]
pub struct Record<'a> {
    pub signature: Signature,
    pub flags: u32,
    pub form_id: u32,
    pub record_version: u16,
    /// The raw body bytes (does not include the 24-byte header).
    /// For compressed records this is still the compressed payload;
    /// decompression is applied separately.
    pub body: &'a [u8],
}

impl<'a> Record<'a> {
    /// `true` iff the compressed-record flag is set.
    pub fn is_compressed(&self) -> bool {
        self.flags & RECORD_FLAG_COMPRESSED != 0
    }

    /// `true` iff the deleted flag is set. Mora skips deleted records.
    pub fn is_deleted(&self) -> bool {
        self.flags & RECORD_FLAG_DELETED != 0
    }
}

/// Parse a record header + body from `bytes` starting at `offset`.
/// Returns `(record, new_offset)` — `new_offset` points to the byte
/// after the record body.
pub fn read_record<'a>(bytes: &'a [u8], offset: usize) -> Result<(Record<'a>, usize), ReadError> {
    let (signature, o) = read_signature(bytes, offset)?;
    let (data_size, o) = le_u32(bytes, o)?;
    let (flags, o) = le_u32(bytes, o)?;
    let (form_id, o) = le_u32(bytes, o)?;
    let (_vc_info, o) = le_u32(bytes, o)?;
    let (record_version, o) = le_u16(bytes, o)?;
    let (_unknown, o) = le_u16(bytes, o)?;

    let body_start = o;
    let body_end = body_start + data_size as usize;
    if body_end > bytes.len() {
        return Err(ReadError::Truncated {
            offset: body_start,
            needed: data_size as usize,
        });
    }
    let body = &bytes[body_start..body_end];

    Ok((
        Record {
            signature,
            flags,
            form_id,
            record_version,
            body,
        },
        body_end,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_record_header(
        sig: &[u8; 4],
        data_size: u32,
        flags: u32,
        form_id: u32,
        version: u16,
    ) -> Vec<u8> {
        let mut v = Vec::with_capacity(RECORD_HEADER_SIZE);
        v.extend_from_slice(sig);
        v.extend_from_slice(&data_size.to_le_bytes());
        v.extend_from_slice(&flags.to_le_bytes());
        v.extend_from_slice(&form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes()); // vc_info
        v.extend_from_slice(&version.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes()); // unknown
        v
    }

    #[test]
    fn parses_minimal_record_header() {
        let mut buf = make_record_header(b"WEAP", 0, 0, 0x0001_2EB7, 44);
        // zero-length body
        let (rec, next) = read_record(&buf, 0).unwrap();
        assert_eq!(rec.signature, Signature::new(b"WEAP"));
        assert_eq!(rec.form_id, 0x0001_2EB7);
        assert_eq!(rec.record_version, 44);
        assert_eq!(rec.body.len(), 0);
        assert_eq!(next, RECORD_HEADER_SIZE);
        buf.clear(); // silence unused
    }

    #[test]
    fn record_with_body() {
        let mut buf = make_record_header(b"WEAP", 6, 0, 0xDEAD_BEEF, 44);
        buf.extend_from_slice(b"abcdef");
        let (rec, next) = read_record(&buf, 0).unwrap();
        assert_eq!(rec.body, b"abcdef");
        assert_eq!(next, RECORD_HEADER_SIZE + 6);
    }

    #[test]
    fn compressed_flag_detected() {
        let buf = make_record_header(b"WEAP", 0, RECORD_FLAG_COMPRESSED, 0, 44);
        let (rec, _) = read_record(&buf, 0).unwrap();
        assert!(rec.is_compressed());
        assert!(!rec.is_deleted());
    }

    #[test]
    fn deleted_flag_detected() {
        let buf = make_record_header(b"WEAP", 0, RECORD_FLAG_DELETED, 0, 44);
        let (rec, _) = read_record(&buf, 0).unwrap();
        assert!(rec.is_deleted());
        assert!(!rec.is_compressed());
    }

    #[test]
    fn truncated_body_errors() {
        let mut buf = make_record_header(b"WEAP", 100, 0, 0, 44);
        buf.extend_from_slice(&[0u8; 10]); // only 10 body bytes, data_size says 100
        match read_record(&buf, 0) {
            Err(ReadError::Truncated { .. }) => {}
            other => panic!("expected Truncated; got {other:?}"),
        }
    }
}
