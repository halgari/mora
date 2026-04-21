//! Subrecord header (6 bytes) + iteration over a record body.
//!
//! Also handles the `XXXX` override for subrecords whose payload
//! size exceeds `u16::MAX`.

use crate::reader::{ReadError, le_u16, le_u32, read_signature};
use crate::signature::{Signature, XXXX};

pub const SUBRECORD_HEADER_SIZE: usize = 6;

#[derive(Debug)]
pub struct Subrecord<'a> {
    pub signature: Signature,
    pub data: &'a [u8],
}

/// Iterator over subrecords in a record body.
pub struct SubrecordIter<'a> {
    bytes: &'a [u8],
    offset: usize,
    /// Pending XXXX override — applied to the next subrecord.
    pending_size: Option<u32>,
}

impl<'a> SubrecordIter<'a> {
    pub fn new(body: &'a [u8]) -> Self {
        SubrecordIter {
            bytes: body,
            offset: 0,
            pending_size: None,
        }
    }
}

impl<'a> Iterator for SubrecordIter<'a> {
    type Item = Result<Subrecord<'a>, ReadError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.offset >= self.bytes.len() {
            return None;
        }
        let start = self.offset;
        let (sig, o) = match read_signature(self.bytes, start) {
            Ok(x) => x,
            Err(e) => return Some(Err(e)),
        };
        let (header_size, o) = match le_u16(self.bytes, o) {
            Ok(x) => x,
            Err(e) => return Some(Err(e)),
        };

        // XXXX override: the XXXX record declares the real size of the
        // NEXT subrecord. XXXX itself has u16 size = 4 and its payload
        // is a u32 real_size.
        if sig == XXXX {
            // Read the u32 real size from the 4-byte XXXX payload.
            if header_size != 4 {
                return Some(Err(ReadError::Truncated {
                    offset: o,
                    needed: 4,
                }));
            }
            let (real_size, next_o) = match le_u32(self.bytes, o) {
                Ok(x) => x,
                Err(e) => return Some(Err(e)),
            };
            self.pending_size = Some(real_size);
            self.offset = next_o;
            return self.next();
        }

        let data_size = self.pending_size.take().unwrap_or(header_size as u32);
        let data_start = o;
        let data_end = data_start + data_size as usize;
        if data_end > self.bytes.len() {
            return Some(Err(ReadError::Truncated {
                offset: data_start,
                needed: data_size as usize,
            }));
        }
        let data = &self.bytes[data_start..data_end];
        self.offset = data_end;

        Some(Ok(Subrecord {
            signature: sig,
            data,
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_subrecord(sig: &[u8; 4], payload: &[u8]) -> Vec<u8> {
        let mut v = Vec::with_capacity(SUBRECORD_HEADER_SIZE + payload.len());
        v.extend_from_slice(sig);
        v.extend_from_slice(&(payload.len() as u16).to_le_bytes());
        v.extend_from_slice(payload);
        v
    }

    #[test]
    fn iterates_two_subrecords() {
        let mut body = make_subrecord(b"EDID", b"IronSword\0");
        body.extend_from_slice(&make_subrecord(b"KWDA", &[0x01u8, 0x02, 0x03, 0x04]));
        let subs: Vec<_> = SubrecordIter::new(&body).collect::<Result<_, _>>().unwrap();
        assert_eq!(subs.len(), 2);
        assert_eq!(subs[0].signature.as_bytes(), *b"EDID");
        assert_eq!(subs[0].data, b"IronSword\0");
        assert_eq!(subs[1].signature.as_bytes(), *b"KWDA");
        assert_eq!(subs[1].data, &[0x01, 0x02, 0x03, 0x04]);
    }

    #[test]
    fn xxxx_override_applied_to_next_subrecord() {
        // XXXX says next subrecord's real size is 100,000 bytes.
        let mut body = Vec::new();
        body.extend_from_slice(b"XXXX");
        body.extend_from_slice(&4u16.to_le_bytes()); // header_size
        body.extend_from_slice(&100_000u32.to_le_bytes()); // real_size
        // Large EDID payload (100,000 bytes of 0xAB).
        body.extend_from_slice(b"EDID");
        body.extend_from_slice(&0u16.to_le_bytes()); // header_size (will be overridden)
        body.extend(std::iter::repeat_n(0xABu8, 100_000));

        let subs: Vec<_> = SubrecordIter::new(&body).collect::<Result<_, _>>().unwrap();
        assert_eq!(subs.len(), 1);
        assert_eq!(subs[0].signature.as_bytes(), *b"EDID");
        assert_eq!(subs[0].data.len(), 100_000);
    }

    #[test]
    fn empty_body_yields_no_subrecords() {
        let body: Vec<u8> = Vec::new();
        let subs: Vec<_> = SubrecordIter::new(&body).collect::<Result<_, _>>().unwrap();
        assert!(subs.is_empty());
    }
}
