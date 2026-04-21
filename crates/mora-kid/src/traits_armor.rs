//! Armor trait parser.
//!
//! Traits from KID's `ArmorTraits`:
//!   HEAVY / LIGHT / CLOTHING
//!   E / -E           (enchanted)
//!   T / -T           (templated)
//!   AR(min max)      (armor rating range)
//!   W(min max)       (weight range)
//!   30-61 (numeric)  (BipedObjectSlot)
//!
//! Same M3 caveat as weapon traits: predicates requiring subrecord
//! data not yet exposed on ArmorRecord log-and-skip at evaluate time.

use crate::traits_weapon::parse_range;
use crate::TraitParseError;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ArmorType {
    Heavy,
    Light,
    Clothing,
}

impl ArmorType {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "HEAVY" => Some(Self::Heavy),
            "LIGHT" => Some(Self::Light),
            "CLOTHING" => Some(Self::Clothing),
            _ => None,
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct ArmorTraits {
    pub armor_types: Vec<ArmorType>,
    pub require_enchanted: Option<bool>,
    pub require_template: Option<bool>,
    pub ar_range: Option<(f32, f32)>,
    pub weight_range: Option<(f32, f32)>,
    pub body_slots: Vec<u8>, // 30..=61
}

impl ArmorTraits {
    pub fn parse(s: &str) -> Result<Self, TraitParseError> {
        let mut out = ArmorTraits::default();
        if s.is_empty() || s.eq_ignore_ascii_case("NONE") {
            return Ok(out);
        }
        for token in s.split(',') {
            let t = token.trim();
            if t.is_empty() {
                continue;
            }
            if let Some(at) = ArmorType::parse(t) {
                out.armor_types.push(at);
                continue;
            }
            match t {
                "E" => out.require_enchanted = Some(true),
                "-E" => out.require_enchanted = Some(false),
                "T" => out.require_template = Some(true),
                "-T" => out.require_template = Some(false),
                _ => {
                    if let Some(range) = parse_range(t, 'A') {
                        // KID uses AR(min max); our parse_range expects a single letter.
                        // Fall through: the helper only strips "A(", but KID's prefix is "AR".
                        // Use a dedicated parse_ar_range below.
                        let _ = range;
                        return Err(TraitParseError::Unknown(t.to_string()));
                    }
                    if let Some(range) = parse_ar_range(t) {
                        out.ar_range = Some(range);
                    } else if let Some(range) = parse_range(t, 'W') {
                        out.weight_range = Some(range);
                    } else if let Some(slot) = parse_body_slot(t) {
                        out.body_slots.push(slot);
                    } else {
                        return Err(TraitParseError::Unknown(t.to_string()));
                    }
                }
            }
        }
        Ok(out)
    }
}

fn parse_ar_range(s: &str) -> Option<(f32, f32)> {
    let after = s.strip_prefix("AR")?;
    let inner = after.strip_prefix('(')?.strip_suffix(')')?;
    let mut parts = inner.split_whitespace();
    let min: f32 = parts.next()?.parse().ok()?;
    let max: f32 = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some((min, max))
}

fn parse_body_slot(s: &str) -> Option<u8> {
    let n: u8 = s.parse().ok()?;
    if (30..=61).contains(&n) {
        Some(n)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_and_none() {
        assert!(ArmorTraits::parse("").unwrap().armor_types.is_empty());
        assert!(ArmorTraits::parse("NONE").unwrap().armor_types.is_empty());
    }

    #[test]
    fn armor_types() {
        let t = ArmorTraits::parse("HEAVY,LIGHT").unwrap();
        assert_eq!(t.armor_types, vec![ArmorType::Heavy, ArmorType::Light]);
    }

    #[test]
    fn ar_and_weight_ranges() {
        let t = ArmorTraits::parse("AR(20 100),W(5 15)").unwrap();
        assert_eq!(t.ar_range, Some((20.0, 100.0)));
        assert_eq!(t.weight_range, Some((5.0, 15.0)));
    }

    #[test]
    fn body_slots() {
        let t = ArmorTraits::parse("32,30,61").unwrap();
        assert_eq!(t.body_slots, vec![32, 30, 61]);
    }

    #[test]
    fn body_slot_out_of_range_is_error() {
        let err = ArmorTraits::parse("29").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
        let err = ArmorTraits::parse("62").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
    }

    #[test]
    fn mixed() {
        let t = ArmorTraits::parse("HEAVY,-E,32,AR(20 100)").unwrap();
        assert_eq!(t.armor_types, vec![ArmorType::Heavy]);
        assert_eq!(t.require_enchanted, Some(false));
        assert_eq!(t.body_slots, vec![32]);
        assert_eq!(t.ar_range, Some((20.0, 100.0)));
    }
}

