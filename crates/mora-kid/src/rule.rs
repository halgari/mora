//! KidRule AST.
//!
//! A `KidRule` captures one INI line after parsing. It is the input
//! to `KidDistributor::lower`.

use crate::reference::Reference;
use crate::traits_armor::ArmorTraits;
use crate::traits_weapon::WeaponTraits;

/// The record type a rule targets.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RecordType {
    Weapon,
    Armor,
    /// Any other type string — parsed but skipped at distribute time.
    Other(String),
}

impl RecordType {
    pub fn parse(s: &str) -> Self {
        match s.trim() {
            "Weapon" => RecordType::Weapon,
            "Armor" => RecordType::Armor,
            other => RecordType::Other(other.to_string()),
        }
    }
}

/// Type-specific trait bag. Only Weapon + Armor populated at M3.
#[derive(Debug, Clone, Default)]
pub enum Traits {
    #[default]
    None,
    Weapon(WeaponTraits),
    Armor(ArmorTraits),
}

/// Filter bucket: each entry is a Reference + optional plain-string
/// target (for string filters that didn't resolve as a form).
#[derive(Debug, Clone, Default)]
pub struct FilterBuckets {
    /// MATCH (OR) — at least one must match
    pub match_: Vec<Reference>,
    /// NOT — item must not match any of these
    pub not: Vec<Reference>,
    /// ALL (AND, `+` prefix) — all must match. Parsed at M3, evaluator
    /// at M3 LOGS-AND-SKIPS — activation is Plan 7+.
    pub all: Vec<Vec<Reference>>,
    /// ANY (`*` prefix, substring) — parsed but evaluator is Plan 7+.
    pub any: Vec<String>,
}

impl FilterBuckets {
    pub fn is_empty(&self) -> bool {
        self.match_.is_empty() && self.not.is_empty() && self.all.is_empty() && self.any.is_empty()
    }

    pub fn has_unsupported(&self) -> bool {
        !self.all.is_empty() || !self.any.is_empty()
    }
}

/// A parsed KID rule.
#[derive(Debug, Clone)]
pub struct KidRule {
    /// The keyword to distribute.
    pub keyword: Reference,
    /// Which record type the rule targets.
    pub record_type: RecordType,
    /// Filters (bucketed by prefix).
    pub filters: FilterBuckets,
    /// Record-type-specific trait filters.
    pub traits: Traits,
    /// Chance percentage (0..=100). 100 (always-pass) is the default.
    pub chance: u8,
    /// Source location for diagnostics.
    pub source: SourceLocation,
}

/// Where a rule came from. Used for error messages.
#[derive(Debug, Clone)]
pub struct SourceLocation {
    pub file: String,
    pub line_number: usize,
}

impl SourceLocation {
    pub const SYNTHETIC: SourceLocation = SourceLocation {
        file: String::new(),
        line_number: 0,
    };
}
