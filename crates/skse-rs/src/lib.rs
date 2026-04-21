//! `skse-rs` — Rust-native SKSE plugin framework.
//!
//! Clean-room Rust port of the SKSE plugin infrastructure Mora needs.
//! Not a binding to the C++ CommonLibSSE-NG library; every ABI type
//! is defined directly in Rust with layout validated by compile-time
//! size asserts.
//!
//! See `docs/src/skse-rs-ffi-reference.md` for the source-of-truth
//! layout reference.
//!
//! This crate is no-std-friendly in principle but currently relies on
//! `std` for logging file I/O.
//!
//! # M1 Foundation scope
//!
//! Plan 2 (this milestone's first half) implements:
//! - ABI types (`PluginVersionData`, `PluginInfo`, `SKSEInterface`,
//!   `SKSEMessagingInterface`, messaging IDs).
//! - The three plugin exports (`SKSEPlugin_Version`,
//!   `SKSEPlugin_Query`, `SKSEPlugin_Load`).
//! - File-based logger writing to the Skyrim log directory.
//! - `SksePlugin` trait + `declare_plugin!` macro for downstream
//!   crates to opt in cleanly.
//!
//! Plan 3 adds: Address Library parser, relocation layer, game type
//! layouts, `TESDataHandler` form lookup, `AddKeyword` re-implementation.

pub mod ffi;
pub mod log;
pub mod plugin;
pub mod version;

pub use plugin::{LoadError, LoadOutcome, SksePlugin};
pub use version::{PluginVersion, RuntimeVersion};

/// Placeholder — real impl lands in Phase D.
#[macro_export]
macro_rules! declare_plugin {
    () => {};
}
