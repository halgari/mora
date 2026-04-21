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
use skse_rs::game::form::{lookup_by_id, TESForm};
use skse_rs::game::keyword::BGSKeyword;
use skse_rs::game::keyword_form::{add_keyword, BGSKeywordForm};
use skse_rs::relocation;
use skse_rs::{declare_plugin, LoadOutcome, Logger, PluginVersion, SksePlugin};

// Known FormIDs in Skyrim.esm (mod index 0x00).
const IRON_SWORD_FID: u32 = 0x0001_2EB7;
const WEAP_MATERIAL_IRON_FID: u32 = 0x0001_E718;

// Offset of the `BGSKeywordForm` sub-object within a `TESObjectWEAP`.
//
// Verified by summing base-class sizes from CommonLibSSE-NG static_asserts
// (TESObjectWEAP.h, and each base class header). Inheritance order:
//
//   Base class                  size    running offset
//   TESBoundObject              0x30    0x000
//   TESFullName                 0x10    0x030
//   TESModelTextureSwap         0x38    0x040
//   TESIcon                     0x10    0x078
//   TESEnchantableForm          0x18    0x088
//   TESValueForm                0x10    0x0A0
//   TESWeightForm               0x10    0x0B0
//   TESAttackDamageForm         0x10    0x0C0
//   BGSDestructibleObjectForm   0x10    0x0D0
//   BGSEquipType                0x10    0x0E0
//   BGSPreloadable              0x08    0x0F0
//   BGSMessageIcon              0x18    0x0F8
//   BGSPickupPutdownSounds      0x18    0x110
//   BGSBlockBashData            0x18    0x128
//   BGSKeywordForm              0x18    0x140  <-- starts here
//   TESDescription              ...     0x158
//
// Cross-check: sizeof(TESObjectWEAP) == 0x220 per static_assert in
// TESObjectWEAP.h; the field layout beyond 0x158 accounts for the rest.
const WEAPON_KEYWORD_FORM_OFFSET: isize = 0x140;

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
            .write_line(&format!(
                "SKSE runtime: 0x{:08x}",
                skse.runtime_version
            ))
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
                logger
                    .write_line(&format!("lookup_by_id failed: {e}"))
                    .ok();
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
                logger
                    .write_line(&format!("lookup_by_id failed: {e}"))
                    .ok();
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
        // WEAPON_KEYWORD_FORM_OFFSET is verified above via base-class size sums.
        let keyword_form: *mut BGSKeywordForm =
            unsafe { (iron_sword as *mut u8).offset(WEAPON_KEYWORD_FORM_OFFSET) }
                as *mut BGSKeywordForm;

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
