//! KID INI parser + distributor frontend.
//!
//! M3 supports Weapon + Armor record types; rules targeting other
//! types are parsed but skipped at distribute time with a warning.
//! See `docs/src/kid-ini-grammar.md` for the full grammar.

pub mod distributor;
pub mod filter;
pub mod ini;
pub mod pipeline;
pub mod reference;
pub mod rule;
pub mod traits_armor;
pub mod traits_weapon;

pub use distributor::{KidDistributor, KidError};
pub use pipeline::{CompileError, compile};
pub use reference::Reference;
pub use rule::{KidRule, RecordType};

/// Error from parsing a trait token.
#[derive(Debug, thiserror::Error)]
pub enum TraitParseError {
    #[error("unknown trait: {0}")]
    Unknown(String),
}
