//! `MoraGoldenHarness` — M4 golden-test capture SKSE plugin.
//!
//! On `kDataLoaded`:
//!   1. Walk every form via `TESDataHandler`'s allForms map.
//!   2. For each WEAP and ARMO, read its `BGSKeywordForm::keywords`
//!      array.
//!   3. Serialize `(form_id → sorted keyword_ids)` as JSONL into
//!      `Data/MoraCache/dumps/{weapons,armors}.jsonl`.
//!   4. Write an empty sentinel file `Data/MoraCache/dumps/.done`.
//!
//! The capture `xtask` polls for `.done` via a `check.sh` hook, then
//! tears the game down. No TCP, no commands, no long-lived listener.
//!
//! This crate is intentionally narrow: it exists to unblock M4 before
//! the full `mora-test-harness` TCP protocol lands in M5. When M5
//! ships, consolidate with (or delete in favor of) the full harness.

#![allow(non_snake_case)]

use std::sync::OnceLock;

use skse_rs::ffi::SKSEInterface;
use skse_rs::relocation;
use skse_rs::{LoadOutcome, Logger, PluginVersion, SksePlugin, declare_plugin};

static LOGGER: OnceLock<Logger> = OnceLock::new();

struct MoraGoldenHarness;

impl SksePlugin for MoraGoldenHarness {
    const NAME: &'static str = "MoraGoldenHarness";
    const AUTHOR: &'static str = "Mora";
    const VERSION: PluginVersion = PluginVersion {
        major: 0,
        minor: 1,
        patch: 0,
        build: 0,
    };

    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome {
        let logger = Logger::open(Self::NAME)?;
        logger.write_line("MoraGoldenHarness loaded").ok();
        logger
            .write_line(&format!("SKSE runtime: 0x{:08x}", skse.runtime_version))
            .ok();
        if let Some(p) = relocation::resolve_default_library_path()
            && let Err(e) = relocation::load_library_from_path(&p)
        {
            logger
                .write_line(&format!("Address Library load FAILED: {e}"))
                .ok();
        }
        let _ = LOGGER.set(logger);
        Ok(())
    }

    unsafe fn on_data_loaded() {
        // Task 4 adds the capture logic here.
    }
}

declare_plugin!(MoraGoldenHarness);
