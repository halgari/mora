//! Memory-mapped ESP/ESL/ESM reader, `plugins.txt` parser, load-order
//! resolver, and indexed record view for Mora.
//!
//! Target: Skyrim Special Edition 1.5.97+ (LZ4-compressed records,
//! record version 44, full + light-slot mod indices).
//!
//! See `docs/src/mora-esp-reference.md` for the binary format + load
//! order rules.

pub mod compression;
pub mod group;
pub mod load_order;
pub mod plugin;
pub mod plugins_txt;
pub mod reader;
pub mod record;
pub mod records;
pub mod signature;
pub mod subrecord;
pub mod subrecords;
pub mod tes4;
pub mod world;

pub use plugin::EspPlugin;
pub use signature::Signature;
pub use world::EspWorld;
