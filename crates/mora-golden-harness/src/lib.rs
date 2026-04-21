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

use std::path::{Path, PathBuf};
use std::sync::OnceLock;

use skse_rs::ffi::SKSEInterface;
use skse_rs::game::form::TESForm;
use skse_rs::game::hash_map::FormHashMap;
use skse_rs::game::keyword_form::{
    ARMOR_KEYWORD_FORM_OFFSET, BGSKeywordForm, WEAPON_KEYWORD_FORM_OFFSET, form_type,
};
use skse_rs::game::lock::{BSReadWriteLock, ReadGuard};
use skse_rs::relocation;
use skse_rs::{LoadOutcome, Logger, PluginVersion, SksePlugin, declare_plugin};

static LOGGER: OnceLock<Logger> = OnceLock::new();

struct Dumps {
    /// Sorted by form_id. Each keyword list is also sorted.
    weapons: Vec<(u32, Vec<u32>)>,
    armors: Vec<(u32, Vec<u32>)>,
}

#[derive(Debug, thiserror::Error)]
enum CaptureError {
    #[error("relocation: {0}")]
    Relocation(#[from] skse_rs::relocation::RelocationError),
    #[error("allForms map pointer was null")]
    NullAllForms,
}

unsafe fn collect_keyword_dumps(logger: &Logger) -> Result<Dumps, CaptureError> {
    // Resolve the global form-map pointer + its lock. Same IDs as
    // skse-rs::game::form::lookup_by_id uses internally.
    let all_forms_pp = skse_rs::relocation::Relocation::id(skse_rs::game::form::ae_ids::ALL_FORMS)?;
    let lock_reloc =
        skse_rs::relocation::Relocation::id(skse_rs::game::form::ae_ids::ALL_FORMS_LOCK)?;
    let all_forms_pp: *mut *mut FormHashMap = unsafe { all_forms_pp.as_mut_ptr() };
    let lock: *mut BSReadWriteLock = unsafe { lock_reloc.as_mut_ptr() };
    let _guard = unsafe { ReadGuard::new(lock)? };
    let map_ptr: *mut FormHashMap = unsafe { *all_forms_pp };
    if map_ptr.is_null() {
        return Err(CaptureError::NullAllForms);
    }
    let map: &FormHashMap = unsafe { &*map_ptr };

    let mut weapons: Vec<(u32, Vec<u32>)> = Vec::new();
    let mut armors: Vec<(u32, Vec<u32>)> = Vec::new();

    for (form_id, form) in unsafe { map.iter() } {
        if form.is_null() {
            continue;
        }
        let ty = unsafe { (*form).form_type };
        let kws = match ty {
            form_type::WEAPON => unsafe { read_keywords(form, WEAPON_KEYWORD_FORM_OFFSET) },
            form_type::ARMOR => unsafe { read_keywords(form, ARMOR_KEYWORD_FORM_OFFSET) },
            _ => continue,
        };
        if kws.is_empty() {
            continue; // omit forms with empty keyword lists
        }
        match ty {
            form_type::WEAPON => weapons.push((form_id, kws)),
            form_type::ARMOR => armors.push((form_id, kws)),
            _ => unreachable!(),
        }
    }

    weapons.sort_by_key(|(fid, _)| *fid);
    armors.sort_by_key(|(fid, _)| *fid);

    logger
        .write_line(&format!(
            "collected: {} weapons / {} armors (non-empty keyword lists)",
            weapons.len(),
            armors.len()
        ))
        .ok();

    Ok(Dumps { weapons, armors })
}

/// Cast `form` to a `*mut BGSKeywordForm` via `offset`, clone its
/// `keywords` array into a sorted `Vec<u32>` of FormIDs.
unsafe fn read_keywords(form: *mut TESForm, offset: isize) -> Vec<u32> {
    let kw_form: *mut BGSKeywordForm =
        unsafe { (form as *mut u8).offset(offset) } as *mut BGSKeywordForm;
    let kw_ref = unsafe { &*kw_form };
    let n = kw_ref.num_keywords as usize;
    if n == 0 || kw_ref.keywords.is_null() {
        return Vec::new();
    }
    let mut out: Vec<u32> = Vec::with_capacity(n);
    for i in 0..n {
        let p = unsafe { *kw_ref.keywords.add(i) };
        if p.is_null() {
            continue;
        }
        // BGSKeyword is-a TESForm; the form_id is at the fixed TESForm
        // offset 0x14.
        let form_ptr: *const TESForm = p as *const TESForm;
        let fid = unsafe { (*form_ptr).form_id };
        out.push(fid);
    }
    out.sort_unstable();
    out.dedup();
    out
}

fn dumps_dir() -> PathBuf {
    // CWD when Skyrim runs is the install root. Data/SKSE/Plugins is
    // the canonical mod dir; we write dumps under
    // Data/MoraCache/dumps/ so the xtask's host-side pull has a
    // fixed path.
    Path::new("Data").join("MoraCache").join("dumps")
}

fn write_dumps(dumps: &Dumps) -> std::io::Result<()> {
    let dir = dumps_dir();
    std::fs::create_dir_all(&dir)?;
    write_jsonl(&dir.join("weapons.jsonl"), &dumps.weapons)?;
    write_jsonl(&dir.join("armors.jsonl"), &dumps.armors)?;
    Ok(())
}

fn write_jsonl(path: &Path, entries: &[(u32, Vec<u32>)]) -> std::io::Result<()> {
    use std::io::Write;
    let f = std::fs::File::create(path)?;
    let mut w = std::io::BufWriter::new(f);
    for (form_id, kws) in entries {
        write!(w, "{{\"form\":\"0x{:08x}\",\"kws\":[", form_id)?;
        for (i, kw) in kws.iter().enumerate() {
            if i > 0 {
                write!(w, ",")?;
            }
            write!(w, "\"0x{:08x}\"", kw)?;
        }
        writeln!(w, "]}}")?;
    }
    w.flush()?;
    Ok(())
}

fn write_done_sentinel() -> std::io::Result<()> {
    let dir = dumps_dir();
    std::fs::create_dir_all(&dir)?;
    // Empty sentinel; presence is the signal.
    std::fs::File::create(dir.join(".done"))?;
    Ok(())
}

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
        let expected_name = relocation::versionlib_filename_for(skse.runtime_version);
        match relocation::resolve_versionlib_for(skse.runtime_version) {
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
                    .write_line(&format!(
                        "Address Library file {expected_name} not found under SKSE/Plugins/"
                    ))
                    .ok();
            }
        }
        let _ = LOGGER.set(logger);
        Ok(())
    }

    unsafe fn on_data_loaded() {
        let Some(logger) = LOGGER.get() else { return };
        logger
            .write_line("kDataLoaded received — waiting for peer plugins to finish")
            .ok();

        // KID (and potentially other SKSE plugins) do their distribution
        // work asynchronously after the kDataLoaded handler returns —
        // LOOKUP builds the rule table inside the handler, DISTRIBUTE
        // applies patches on subsequent ticks. If we dump immediately,
        // we see vanilla+override-only keyword lists. A 20-second sleep
        // gives KID 3.4.0 ample time to complete its distribution on
        // Skyrim SE 1.6.1179. This is brittle but effective; a better
        // fix would poll KID's log for a DISTRIBUTE-complete marker.
        std::thread::sleep(std::time::Duration::from_secs(20));
        logger.write_line("wait complete — beginning capture").ok();

        let collected = match unsafe { collect_keyword_dumps(logger) } {
            Ok(x) => x,
            Err(e) => {
                logger.write_line(&format!("capture failed: {e}")).ok();
                return;
            }
        };

        if let Err(e) = write_dumps(&collected) {
            logger.write_line(&format!("write_dumps failed: {e}")).ok();
            return;
        }

        if let Err(e) = write_done_sentinel() {
            logger
                .write_line(&format!("write_done_sentinel failed: {e}"))
                .ok();
            return;
        }

        logger
            .write_line(&format!(
                "capture OK: {} weapons / {} armors",
                collected.weapons.len(),
                collected.armors.len()
            ))
            .ok();
    }
}

declare_plugin!(MoraGoldenHarness);
