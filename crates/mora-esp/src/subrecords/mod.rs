//! Typed subrecord parsers.
//!
//! Each module exposes a `parse(&[u8]) -> Result<T, Err>` function.
//! Callers obtain subrecord bytes from `SubrecordIter` and pass to
//! the typed parser.

pub mod edid;
pub mod kwda;
pub mod form_id_ref;
pub mod weapon_data;
pub mod weapon_dnam;
pub mod armor_data;
pub mod armor_dnam;
pub mod biped_object;
