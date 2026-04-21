//! Weapon trait parser.
//!
//! Traits from KID's `WeaponTraits`:
//!   OneHandSword / OneHandDagger / OneHandAxe / OneHandMace
//!   TwoHandSword / TwoHandAxe / Bow / Crossbow / Staff / HandToHandMelee
//!   E / -E           (enchanted / not)
//!   T / -T           (template / no template)
//!   D(min max)       (damage range)
//!   W(min max)       (weight range)
//!
//! M3 parses these into a struct; the distributor uses them but
//! note that many require subrecord fields (DNAM for damage, OBND
//! for weight, etc.) that the current WeaponRecord does NOT expose.
//! Unsupported trait predicates log-and-skip at evaluate time.

use crate::TraitParseError;

/// Animation type (weapon type discriminator).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WeaponAnimType {
    OneHandSword,
    OneHandDagger,
    OneHandAxe,
    OneHandMace,
    TwoHandSword,
    TwoHandAxe,
    Bow,
    Crossbow,
    Staff,
    HandToHandMelee,
}

impl WeaponAnimType {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "OneHandSword" => Some(Self::OneHandSword),
            "OneHandDagger" => Some(Self::OneHandDagger),
            "OneHandAxe" => Some(Self::OneHandAxe),
            "OneHandMace" => Some(Self::OneHandMace),
            "TwoHandSword" => Some(Self::TwoHandSword),
            "TwoHandAxe" => Some(Self::TwoHandAxe),
            "Bow" => Some(Self::Bow),
            "Crossbow" => Some(Self::Crossbow),
            "Staff" => Some(Self::Staff),
            "HandToHandMelee" => Some(Self::HandToHandMelee),
            _ => None,
        }
    }
}

/// Parsed weapon trait filters. All fields default to "no constraint".
#[derive(Debug, Clone, Default)]
pub struct WeaponTraits {
    pub anim_types: Vec<WeaponAnimType>,
    pub require_enchanted: Option<bool>, // E (true), -E (false), None (no constraint)
    pub require_template: Option<bool>,  // T / -T
    pub damage_range: Option<(f32, f32)>,
    pub weight_range: Option<(f32, f32)>,
}

impl WeaponTraits {
    /// Parse a comma-separated traits string into a WeaponTraits.
    pub fn parse(s: &str) -> Result<Self, TraitParseError> {
        let mut out = WeaponTraits::default();
        if s.is_empty() || s.eq_ignore_ascii_case("NONE") {
            return Ok(out);
        }
        for token in s.split(',') {
            let t = token.trim();
            if t.is_empty() {
                continue;
            }
            if let Some(anim) = WeaponAnimType::parse(t) {
                out.anim_types.push(anim);
                continue;
            }
            match t {
                "E" => out.require_enchanted = Some(true),
                "-E" => out.require_enchanted = Some(false),
                "T" => out.require_template = Some(true),
                "-T" => out.require_template = Some(false),
                _ => {
                    if let Some(range) = parse_range(t, 'D') {
                        out.damage_range = Some(range);
                    } else if let Some(range) = parse_range(t, 'W') {
                        out.weight_range = Some(range);
                    } else {
                        return Err(TraitParseError::Unknown(t.to_string()));
                    }
                }
            }
        }
        Ok(out)
    }
}

/// Parse a range like `D(min max)` where `D` is the expected letter.
/// Returns None if the token doesn't match this form.
pub(crate) fn parse_range(s: &str, letter: char) -> Option<(f32, f32)> {
    let after_letter = s.strip_prefix(letter)?;
    let inner = after_letter.strip_prefix('(')?.strip_suffix(')')?;
    let mut parts = inner.split_whitespace();
    let min: f32 = parts.next()?.parse().ok()?;
    let max: f32 = parts.next()?.parse().ok()?;
    if parts.next().is_some() {
        return None;
    }
    Some((min, max))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_traits() {
        let t = WeaponTraits::parse("").unwrap();
        assert!(t.anim_types.is_empty());
        assert_eq!(t.require_enchanted, None);
    }

    #[test]
    fn none_is_empty() {
        let t = WeaponTraits::parse("NONE").unwrap();
        assert!(t.anim_types.is_empty());
    }

    #[test]
    fn anim_types() {
        let t = WeaponTraits::parse("OneHandSword,Bow").unwrap();
        assert_eq!(
            t.anim_types,
            vec![WeaponAnimType::OneHandSword, WeaponAnimType::Bow]
        );
    }

    #[test]
    fn enchantment_flags() {
        let t = WeaponTraits::parse("-E").unwrap();
        assert_eq!(t.require_enchanted, Some(false));
        let t = WeaponTraits::parse("E").unwrap();
        assert_eq!(t.require_enchanted, Some(true));
    }

    #[test]
    fn damage_range() {
        let t = WeaponTraits::parse("D(10 20)").unwrap();
        assert_eq!(t.damage_range, Some((10.0, 20.0)));
    }

    #[test]
    fn mixed() {
        let t = WeaponTraits::parse("Bow,-E,D(5 15)").unwrap();
        assert_eq!(t.anim_types, vec![WeaponAnimType::Bow]);
        assert_eq!(t.require_enchanted, Some(false));
        assert_eq!(t.damage_range, Some((5.0, 15.0)));
    }

    #[test]
    fn unknown_errors() {
        let err = WeaponTraits::parse("NotATrait").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
    }

    #[test]
    fn case_sensitive() {
        // KID: traits are case-sensitive via const_hash.
        // "onehandsword" (lowercase) is not recognized.
        let err = WeaponTraits::parse("onehandsword").unwrap_err();
        assert!(matches!(err, TraitParseError::Unknown(_)));
    }
}
