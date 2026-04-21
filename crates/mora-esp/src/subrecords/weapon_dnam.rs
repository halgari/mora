//! WEAP DNAM subrecord — 100 bytes. Mora reads only the subset
//! needed for KID trait predicates:
//!   offset 0x00: animation_type (u8)
//!   offset 0x04: speed (f32)
//!   offset 0x08: reach (f32)
//! Everything else is skipped.

use crate::reader::{ReadError, le_f32, le_u8};

/// Weapon animation type (wire-format enum).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WeaponAnimType {
    HandToHandMelee = 0,
    OneHandSword = 1,
    OneHandDagger = 2,
    OneHandAxe = 3,
    OneHandMace = 4,
    TwoHandSword = 5,
    TwoHandAxe = 6,
    Bow = 7,
    Staff = 8,
    Crossbow = 9,
}

impl WeaponAnimType {
    pub fn from_u8(n: u8) -> Option<Self> {
        match n {
            0 => Some(Self::HandToHandMelee),
            1 => Some(Self::OneHandSword),
            2 => Some(Self::OneHandDagger),
            3 => Some(Self::OneHandAxe),
            4 => Some(Self::OneHandMace),
            5 => Some(Self::TwoHandSword),
            6 => Some(Self::TwoHandAxe),
            7 => Some(Self::Bow),
            8 => Some(Self::Staff),
            9 => Some(Self::Crossbow),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct WeaponDnam {
    pub animation_type: Option<WeaponAnimType>,
    pub speed: f32,
    pub reach: f32,
}

pub fn parse(data: &[u8]) -> Result<WeaponDnam, ReadError> {
    // M3 reads only the first 12 bytes (anim_type + padding + speed + reach).
    // Full DNAM is 100 bytes; we tolerate any size >= 12.
    if data.len() < 12 {
        return Err(ReadError::Truncated {
            offset: data.len(),
            needed: 12,
        });
    }
    let (anim_u8, _) = le_u8(data, 0)?;
    let (speed, _) = le_f32(data, 4)?;
    let (reach, _) = le_f32(data, 8)?;
    Ok(WeaponDnam {
        animation_type: WeaponAnimType::from_u8(anim_u8),
        speed,
        reach,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn build(anim: u8, speed: f32, reach: f32) -> Vec<u8> {
        let mut v = Vec::new();
        v.push(anim);
        v.extend_from_slice(&[0u8; 3]); // padding
        v.extend_from_slice(&speed.to_le_bytes());
        v.extend_from_slice(&reach.to_le_bytes());
        v.extend_from_slice(&[0u8; 88]); // pad to 100
        v
    }

    #[test]
    fn parses_one_hand_sword() {
        let b = build(1, 1.0, 0.75);
        let d = parse(&b).unwrap();
        assert_eq!(d.animation_type, Some(WeaponAnimType::OneHandSword));
        assert_eq!(d.speed, 1.0);
        assert_eq!(d.reach, 0.75);
    }

    #[test]
    fn unknown_anim_type_is_none() {
        let b = build(99, 1.0, 1.0);
        let d = parse(&b).unwrap();
        assert_eq!(d.animation_type, None);
    }

    #[test]
    fn tolerates_short_but_parses_prefix() {
        // Exactly 12 bytes — enough for our prefix.
        let mut b = vec![1u8, 0, 0, 0];
        b.extend_from_slice(&1.5f32.to_le_bytes());
        b.extend_from_slice(&2.0f32.to_le_bytes());
        let d = parse(&b).unwrap();
        assert_eq!(d.animation_type, Some(WeaponAnimType::OneHandSword));
        assert_eq!(d.speed, 1.5);
    }
}
