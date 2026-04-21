//! Partial Rust bindings to Skyrim game types.
//!
//! Every struct in this module carries an `M1-minimal` comment: only
//! the fields and methods the M1 smoke test exercises are defined.
//! Extend when real consumers (M5 mora-runtime, M6 KID frontend) land.

pub mod data_handler;
pub mod form;
pub mod hash_map;
pub mod keyword;
pub mod keyword_form;
pub mod lock;
pub mod memory;
