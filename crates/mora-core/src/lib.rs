//! Core shared types + algorithms for Mora.
//!
//! Frontend crates (`mora-kid`, future `mora-spid`, …) use this crate
//! for the `Patch` types + `Distributor` trait. The runtime uses it
//! for `PatchFile` deserialization. `mora-esp` uses it for `FormId`.
//!
//! No `unsafe`, no platform-specific code — pure Rust logic.
//!
//! See `docs/src/mora-core-reference.md` for types + algorithms.

pub mod chance;
pub mod distributor;
pub mod form_id;
pub mod patch;
pub mod patch_sink;

pub use chance::DeterministicChance;
pub use distributor::{Distributor, DistributorStats};
pub use form_id::{FormId, FullFormId};
pub use patch::{PATCH_FILE_MAGIC, PATCH_FILE_VERSION, Patch, PatchFile};
pub use patch_sink::PatchSink;
