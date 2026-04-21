//! `SkseRsSmoke` — end-to-end smoke plugin.
//!
//! Exercises the full `skse-rs` M1 surface:
//!   - plugin load + log init + address library load
//!   - kDataLoaded listener registration
//!   - form lookup by FormID
//!   - AddKeyword via Skyrim's MemoryManager

#![allow(non_snake_case)]

use std::sync::OnceLock;

use skse_rs::ffi::SKSEInterface;
use skse_rs::game::form::{TESForm, lookup_by_id};
use skse_rs::game::keyword::BGSKeyword;
use skse_rs::game::keyword_form::{BGSKeywordForm, add_keyword};
use skse_rs::relocation;
use skse_rs::{LoadOutcome, Logger, PluginVersion, SksePlugin, declare_plugin};

// Known FormIDs in Skyrim.esm (mod index 0x00).
const IRON_SWORD_FID: u32 = 0x0001_2EB7;
const WEAP_MATERIAL_IRON_FID: u32 = 0x0001_E718;

static LOGGER: OnceLock<Logger> = OnceLock::new();

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
            .write_line(&format!("SKSE runtime: 0x{:08x}", skse.runtime_version))
            .ok();

        match relocation::resolve_default_library_path() {
            Some(p) => match relocation::load_library_from_path(&p) {
                Ok(()) => {
                    logger
                        .write_line(&format!("Address Library loaded from {}", p.display()))
                        .ok();
                }
                Err(e) => {
                    logger
                        .write_line(&format!("Address Library load FAILED: {e}"))
                        .ok();
                }
            },
            None => {
                logger
                    .write_line("Address Library file not found at default path")
                    .ok();
            }
        }

        let _ = LOGGER.set(logger);
        Ok(())
    }

    unsafe fn on_data_loaded() {
        let Some(logger) = LOGGER.get() else { return };
        logger.write_line("kDataLoaded received").ok();

        let iron_sword: *mut TESForm = match unsafe { lookup_by_id(IRON_SWORD_FID) } {
            Ok(Some(p)) => p,
            Ok(None) => {
                logger.write_line("Iron Sword (0x00012EB7) not found").ok();
                return;
            }
            Err(e) => {
                logger.write_line(&format!("lookup_by_id failed: {e}")).ok();
                return;
            }
        };
        logger
            .write_line(&format!(
                "Iron Sword lookup: 0x{IRON_SWORD_FID:08X} -> {iron_sword:?}"
            ))
            .ok();

        let kw_form: *mut TESForm = match unsafe { lookup_by_id(WEAP_MATERIAL_IRON_FID) } {
            Ok(Some(p)) => p,
            Ok(None) => {
                logger
                    .write_line("WeapMaterialIron (0x0001E718) not found")
                    .ok();
                return;
            }
            Err(e) => {
                logger.write_line(&format!("lookup_by_id failed: {e}")).ok();
                return;
            }
        };
        logger
            .write_line(&format!(
                "WeapMaterialIron lookup: 0x{WEAP_MATERIAL_IRON_FID:08X} -> {kw_form:?}"
            ))
            .ok();

        let kw: *mut BGSKeyword = kw_form as *mut BGSKeyword;

        // Cast the TESObjectWEAP pointer to its BGSKeywordForm sub-object.
        // Offset is documented + derived in
        // skse_rs::game::keyword_form::WEAPON_KEYWORD_FORM_OFFSET.
        let keyword_form: *mut BGSKeywordForm =
            unsafe {
                (iron_sword as *mut u8)
                    .offset(skse_rs::game::keyword_form::WEAPON_KEYWORD_FORM_OFFSET)
            } as *mut BGSKeywordForm;

        match unsafe { add_keyword(keyword_form, kw) } {
            Ok(true) => logger.write_line("AddKeyword result: added").ok(),
            Ok(false) => logger.write_line("AddKeyword result: already-present").ok(),
            Err(e) => logger.write_line(&format!("AddKeyword error: {e}")).ok(),
        };

        let num_now = unsafe { (*keyword_form).num_keywords };
        logger
            .write_line(&format!("verify readback: num_keywords = {num_now}"))
            .ok();
        logger.write_line("smoke OK").ok();
    }
}

declare_plugin!(SkseRsSmoke);
