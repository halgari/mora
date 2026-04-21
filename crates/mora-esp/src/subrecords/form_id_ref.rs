//! Generic single-FormID subrecord parser.
//!
//! Used for EITM, CNAM, TNAM — all 4-byte LE u32 FormIDs pointing
//! to another form. Returns the LOCAL FormID (pre-resolution);
//! callers apply `EspWorld::resolve_in_plugin` to promote to the
//! runtime FormId.

use crate::reader::ReadError;

/// Parse a 4-byte LE FormID from a subrecord payload.
pub fn parse(data: &[u8]) -> Result<u32, ReadError> {
    if data.len() < 4 {
        return Err(ReadError::Truncated {
            offset: 0,
            needed: 4,
        });
    }
    Ok(u32::from_le_bytes(data[..4].try_into().unwrap()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_happy() {
        let bytes = [0xB7, 0x2E, 0x01, 0x00];
        let fid = parse(&bytes).unwrap();
        assert_eq!(fid, 0x0001_2EB7);
    }

    #[test]
    fn rejects_too_short() {
        let bytes = [0x01, 0x02, 0x03];
        assert!(parse(&bytes).is_err());
    }
}
