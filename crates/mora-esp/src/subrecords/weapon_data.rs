//! WEAP DATA subrecord — 10 bytes: value (u32) + weight (f32) + damage (u16).

use crate::reader::{ReadError, le_f32, le_u16, le_u32};

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct WeaponData {
    pub value: u32,
    pub weight: f32,
    pub damage: u16,
}

pub fn parse(data: &[u8]) -> Result<WeaponData, ReadError> {
    if data.len() < 10 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 10,
        });
    }
    let (value, o) = le_u32(data, 0)?;
    let (weight, o) = le_f32(data, o)?;
    let (damage, _) = le_u16(data, o)?;
    Ok(WeaponData {
        value,
        weight,
        damage,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_known_layout() {
        let mut bytes = Vec::new();
        bytes.extend_from_slice(&75u32.to_le_bytes()); // value
        bytes.extend_from_slice(&9.0f32.to_le_bytes()); // weight
        bytes.extend_from_slice(&7u16.to_le_bytes()); // damage
        let d = parse(&bytes).unwrap();
        assert_eq!(d.value, 75);
        assert_eq!(d.weight, 9.0);
        assert_eq!(d.damage, 7);
    }

    #[test]
    fn rejects_short() {
        let bytes = [0u8; 5];
        assert!(parse(&bytes).is_err());
    }
}
