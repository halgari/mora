//! `KWDA` — keyword form-id array.
//!
//! Raw layout: sequence of 32-bit LE FormIDs (local — needs remapping
//! via the containing plugin's master list).

use crate::reader::{ReadError, le_u32};

/// Parse a KWDA payload into a `Vec<u32>` of local FormIDs.
pub fn parse(data: &[u8]) -> Result<Vec<u32>, ReadError> {
    if !data.len().is_multiple_of(4) {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 4 - (data.len() % 4),
        });
    }
    let mut out = Vec::with_capacity(data.len() / 4);
    let mut o = 0;
    while o < data.len() {
        let (id, next) = le_u32(data, o)?;
        out.push(id);
        o = next;
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_three_ids() {
        let data = [
            0x11u8, 0x22, 0x33, 0x44, // 0x44332211
            0xAA, 0xBB, 0xCC, 0xDD, // 0xDDCCBBAA
            0x00, 0x00, 0x00, 0x00, // 0x00000000
        ];
        let ids = parse(&data).unwrap();
        assert_eq!(ids, vec![0x44332211, 0xDDCCBBAA, 0x00000000]);
    }

    #[test]
    fn empty_payload_is_ok() {
        let ids = parse(&[]).unwrap();
        assert!(ids.is_empty());
    }

    #[test]
    fn unaligned_payload_errors() {
        let data = [0u8, 1, 2]; // 3 bytes, not a multiple of 4
        assert!(parse(&data).is_err());
    }
}
