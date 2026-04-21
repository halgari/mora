//! 4-byte ASCII signature type + well-known constants.

/// A 4-byte ASCII signature used for records, groups, and subrecords.
///
/// Examples: `TES4`, `WEAP`, `ARMO`, `EDID`, `KWDA`, `GRUP`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Signature(pub [u8; 4]);

impl Signature {
    pub const fn new(bytes: &[u8; 4]) -> Self {
        Signature([bytes[0], bytes[1], bytes[2], bytes[3]])
    }

    pub const fn as_bytes(self) -> [u8; 4] {
        self.0
    }

    /// View as an ASCII string. Signatures are always valid ASCII in
    /// well-formed plugins.
    pub fn as_str(&self) -> &str {
        // SAFETY: signatures are ASCII by definition. Malformed input
        // gets checked at parse time; this display accessor trusts the
        // signature was validated upstream.
        core::str::from_utf8(&self.0).unwrap_or("????")
    }
}

impl std::fmt::Display for Signature {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.as_str())
    }
}

// Well-known signatures.

pub const TES4: Signature = Signature::new(b"TES4");
pub const GRUP: Signature = Signature::new(b"GRUP");

// Record types (subset — M2 scope)
pub const WEAP: Signature = Signature::new(b"WEAP");
pub const ARMO: Signature = Signature::new(b"ARMO");

// Subrecord types (subset)
pub const HEDR: Signature = Signature::new(b"HEDR");
pub const MAST: Signature = Signature::new(b"MAST");
pub const DATA: Signature = Signature::new(b"DATA");
pub const EDID: Signature = Signature::new(b"EDID");
pub const KWDA: Signature = Signature::new(b"KWDA");
pub const XXXX: Signature = Signature::new(b"XXXX");

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn well_known_signatures_display() {
        assert_eq!(TES4.to_string(), "TES4");
        assert_eq!(WEAP.to_string(), "WEAP");
        assert_eq!(EDID.to_string(), "EDID");
    }

    #[test]
    fn as_bytes_roundtrip() {
        let s = Signature::new(b"TEST");
        assert_eq!(s.as_bytes(), *b"TEST");
    }

    #[test]
    fn equality() {
        assert_eq!(Signature::new(b"WEAP"), WEAP);
        assert_ne!(WEAP, ARMO);
    }
}
