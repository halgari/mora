//! `EDID` — editor ID (NUL-terminated ASCII string).

use crate::reader::{read_cstr, ReadError};

/// Parse an EDID subrecord payload into an owned string.
pub fn parse(data: &[u8]) -> Result<String, ReadError> {
    let (s, _) = read_cstr(data, 0, data.len())?;
    Ok(s.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_nul_terminated() {
        let data = b"IronSword\0";
        assert_eq!(parse(data).unwrap(), "IronSword");
    }

    #[test]
    fn handles_empty_string() {
        let data = b"\0";
        assert_eq!(parse(data).unwrap(), "");
    }

    #[test]
    fn missing_nul_errors() {
        let data = b"NoNul";
        assert!(parse(data).is_err());
    }
}
