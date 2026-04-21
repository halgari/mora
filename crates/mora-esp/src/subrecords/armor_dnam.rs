//! ARMO DNAM subrecord — 4 bytes. Raw value is `display × 100`
//! (i32 on disk); exposed as f32 in display scale.

use crate::reader::{ReadError, le_u32};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ArmorDnam {
    /// Display-scale armor rating (raw / 100).
    pub armor_rating: f32,
}

pub fn parse(data: &[u8]) -> Result<ArmorDnam, ReadError> {
    if data.len() < 4 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 4,
        });
    }
    let (raw_u, _) = le_u32(data, 0)?;
    let raw_i = raw_u as i32;
    Ok(ArmorDnam {
        armor_rating: raw_i as f32 / 100.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_glass_chestplate() {
        // Glass chestplate 38.0 display = 3800 raw
        let bytes = 3800u32.to_le_bytes();
        let d = parse(&bytes).unwrap();
        assert_eq!(d.armor_rating, 38.0);
    }

    #[test]
    fn parses_zero() {
        let bytes = 0u32.to_le_bytes();
        assert_eq!(parse(&bytes).unwrap().armor_rating, 0.0);
    }

    #[test]
    fn rejects_short() {
        let bytes = [0u8; 2];
        assert!(parse(&bytes).is_err());
    }
}
