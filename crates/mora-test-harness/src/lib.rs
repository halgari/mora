//! MoraTestHarness SKSE plugin — opens TCP 127.0.0.1:9742, exposes
//! commands for bash-hook integration tests (`status`, `dump <type>`,
//! `lookup <fid>`, `quit`).
//!
//! Populated in milestone M5 (ports the existing C++ harness's protocol
//! to Rust).

// The cdylib output must be named MoraTestHarness (SKSE plugin convention).
#![allow(non_snake_case)]
