//! Top-level compile pipeline. Combines INI discovery, parsing,
//! and distribution into one `compile()` call.

use std::path::Path;

use mora_core::{DeterministicChance, Distributor, Patch, PatchSink};
use mora_esp::EspWorld;

use crate::distributor::{KidDistributor, KidError};
use crate::ini::{self, IniError};

#[derive(Debug, thiserror::Error)]
pub enum CompileError {
    #[error("ini: {0}")]
    Ini(#[from] IniError),
    #[error("kid: {0}")]
    Kid(#[from] KidError),
}

/// Compile: discover all `*_KID.ini` in the data dir, parse rules,
/// run the distributor against `world`, return the finalized
/// `Vec<Patch>`.
///
/// `data_dir` is scanned for INI files; `world` supplies the loaded
/// plugins.
pub fn compile(data_dir: &Path, world: &EspWorld) -> Result<Vec<Patch>, CompileError> {
    let ini_paths =
        ini::discover_kid_ini_files(data_dir).map_err(|e| CompileError::Ini(IniError::Io(e)))?;
    let mut all_rules = Vec::new();
    let mut all_groups = Vec::new();
    for p in &ini_paths {
        let parsed = ini::parse_file(p)?;
        all_rules.extend(parsed.rules);
        all_groups.extend(parsed.exclusive_groups);
    }

    let distributor = KidDistributor::new(all_rules).with_exclusive_groups(all_groups);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    distributor.lower(world, &chance, &mut sink)?;
    let file = sink.finalize();
    Ok(file.patches)
}
