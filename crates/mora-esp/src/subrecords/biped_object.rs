//! `BOD2` / `BODT` subrecords — biped object slots + armor type.
//!
//! BOD2 (SSE primary) is 8 bytes: slots (u32) + armor_type (u32).
//! BODT (legacy) is 12 bytes: slots (u32) + general_flags (u8) +
//! 3 bytes padding + armor_type (u32). Mora parses both into the
//! same BipedObject struct; BOD2 preferred when both present.

use crate::reader::{ReadError, le_u32};

/// Armor type enum (u32 on disk).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArmorType {
    LightArmor = 0,
    HeavyArmor = 1,
    Clothing = 2,
}

impl ArmorType {
    pub fn from_u32(n: u32) -> Option<Self> {
        match n {
            0 => Some(Self::LightArmor),
            1 => Some(Self::HeavyArmor),
            2 => Some(Self::Clothing),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct BipedObject {
    /// 32-bit bitmask; bit N = slot (30 + N).
    pub slots_bitmask: u32,
    pub armor_type: Option<ArmorType>,
}

impl BipedObject {
    /// Slot numbers currently occupied, decoded from `slots_bitmask`.
    /// Returns numbers in the range 30..=61.
    pub fn slot_numbers(&self) -> Vec<u8> {
        (0..32)
            .filter(|bit| self.slots_bitmask & (1u32 << bit) != 0)
            .map(|bit| 30 + bit as u8)
            .collect()
    }
}

/// Parse a BOD2 payload (8 bytes).
pub fn parse_bod2(data: &[u8]) -> Result<BipedObject, ReadError> {
    if data.len() < 8 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 8,
        });
    }
    let (slots, o) = le_u32(data, 0)?;
    let (at, _) = le_u32(data, o)?;
    Ok(BipedObject {
        slots_bitmask: slots,
        armor_type: ArmorType::from_u32(at),
    })
}

/// Parse a BODT payload (12 bytes).
pub fn parse_bodt(data: &[u8]) -> Result<BipedObject, ReadError> {
    if data.len() < 12 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 12,
        });
    }
    let (slots, _) = le_u32(data, 0)?;
    // Skip byte at 0x04 (general_flags) + 3 bytes padding.
    let (at, _) = le_u32(data, 8)?;
    Ok(BipedObject {
        slots_bitmask: slots,
        armor_type: ArmorType::from_u32(at),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bod2_parses_body_slot_heavy() {
        let mut b = Vec::new();
        b.extend_from_slice(&0x00000004u32.to_le_bytes()); // bit 2 = slot 32 (body)
        b.extend_from_slice(&1u32.to_le_bytes()); // heavy
        let o = parse_bod2(&b).unwrap();
        assert_eq!(o.slot_numbers(), vec![32]);
        assert_eq!(o.armor_type, Some(ArmorType::HeavyArmor));
    }

    #[test]
    fn bodt_skips_general_flags_byte() {
        let mut b = Vec::new();
        b.extend_from_slice(&0x00000008u32.to_le_bytes()); // bit 3 = slot 33 (hands)
        b.push(0xFF); // general_flags (ignored)
        b.extend_from_slice(&[0u8; 3]); // padding
        b.extend_from_slice(&0u32.to_le_bytes()); // light armor
        let o = parse_bodt(&b).unwrap();
        assert_eq!(o.slot_numbers(), vec![33]);
        assert_eq!(o.armor_type, Some(ArmorType::LightArmor));
    }

    #[test]
    fn multiple_slots() {
        let mut b = Vec::new();
        // bits 2 + 9 + 12 → slots 32, 39, 42 (body, shield, circlet)
        b.extend_from_slice(&0x00001204u32.to_le_bytes());
        b.extend_from_slice(&2u32.to_le_bytes()); // clothing
        let o = parse_bod2(&b).unwrap();
        assert_eq!(o.slot_numbers(), vec![32, 39, 42]);
        assert_eq!(o.armor_type, Some(ArmorType::Clothing));
    }

    #[test]
    fn unknown_armor_type() {
        let mut b = Vec::new();
        b.extend_from_slice(&0u32.to_le_bytes());
        b.extend_from_slice(&99u32.to_le_bytes()); // unknown
        let o = parse_bod2(&b).unwrap();
        assert_eq!(o.armor_type, None);
    }

    #[test]
    fn bod2_rejects_short() {
        let bytes = [0u8; 4];
        assert!(parse_bod2(&bytes).is_err());
    }

    #[test]
    fn bodt_rejects_short() {
        let bytes = [0u8; 8];
        assert!(parse_bodt(&bytes).is_err());
    }
}
