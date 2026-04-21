//! ARMO DATA subrecord — 8 bytes: value (i32) + weight (f32).

use crate::reader::{ReadError, le_f32, le_u32};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ArmorData {
    pub value: i32,
    pub weight: f32,
}

pub fn parse(data: &[u8]) -> Result<ArmorData, ReadError> {
    if data.len() < 8 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 8,
        });
    }
    let (value_u, o) = le_u32(data, 0)?;
    let (weight, _) = le_f32(data, o)?;
    Ok(ArmorData {
        value: value_u as i32,
        weight,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_value_weight() {
        let mut b = Vec::new();
        b.extend_from_slice(&(125i32 as u32).to_le_bytes());
        b.extend_from_slice(&6.0f32.to_le_bytes());
        let d = parse(&b).unwrap();
        assert_eq!(d.value, 125);
        assert_eq!(d.weight, 6.0);
    }

    #[test]
    fn rejects_short() {
        let bytes = [0u8; 4];
        assert!(parse(&bytes).is_err());
    }

    #[test]
    fn parses_negative_value() {
        // Ensure signed reinterpretation works.
        let mut b = Vec::new();
        b.extend_from_slice(&(0u32.wrapping_sub(1)).to_le_bytes()); // -1 as u32
        b.extend_from_slice(&0.0f32.to_le_bytes());
        let d = parse(&b).unwrap();
        assert_eq!(d.value, -1);
    }
}
