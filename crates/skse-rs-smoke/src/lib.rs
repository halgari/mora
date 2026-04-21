//! `SkseRsSmoke` — smoke-test SKSE plugin exercising `skse-rs`.
//!
//! The smallest plugin that actually *does* something observable:
//! opens a log, writes "Hello from skse-rs" and a timestamp, exits.
//! Used to verify that a pure-Rust SKSE plugin loads, runs, and
//! reports successfully inside a live Skyrim via the SKSE loader.
//!
//! Log path:
//! `<Documents>\My Games\Skyrim Special Edition\SKSE\SkseRsSmoke.log`

// The cdylib output must be named SkseRsSmoke (SKSE plugin convention).
#![allow(non_snake_case)]

use skse_rs::ffi::SKSEInterface;
use skse_rs::{declare_plugin, LoadOutcome, Logger, PluginVersion, SksePlugin};

struct SkseRsSmoke;

impl SksePlugin for SkseRsSmoke {
    const NAME: &'static str = "SkseRsSmoke";
    const AUTHOR: &'static str = "Mora / skse-rs";
    const VERSION: PluginVersion = PluginVersion {
        major: 0,
        minor: 1,
        patch: 0,
        build: 0,
    };

    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome {
        let logger = Logger::open(Self::NAME)?;
        logger.write_line("Hello from skse-rs").ok();
        logger
            .write_line(&format!(
                "SKSE runtime version: 0x{:08x}",
                skse.runtime_version
            ))
            .ok();
        logger.write_line(&format!("plugin log: {}", logger.path().display())).ok();
        Ok(())
    }
}

declare_plugin!(SkseRsSmoke);
