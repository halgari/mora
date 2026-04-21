//! Typed record-body accessors.
//!
//! Each module takes a `Record<'a>` and exposes named fields parsed
//! from the subrecord stream, handling compressed records transparently.

pub mod armor;
pub mod keyword;
pub mod weapon;
