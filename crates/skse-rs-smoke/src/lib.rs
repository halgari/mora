//! Smoke-test plugin for `skse-rs`.
//!
//! The smallest useful SKSE plugin: loads successfully, writes one
//! line to its log file, returns. Exercises everything in `skse-rs`
//! Plan 2 end-to-end.
//!
//! Populated in Phase E/F of Plan 2.

// The cdylib output must be named SkseRsSmoke (SKSE plugin convention).
#![allow(non_snake_case)]
