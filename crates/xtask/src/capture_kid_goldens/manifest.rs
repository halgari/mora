//! Manifest generation: SHA256 over every active ESP + KID + Skyrim
//! versions, serialized to `tests/golden-data/expected/<scenario>/manifest.json`.
//!
//! Task 8 fills this in properly; this stub lets Task 7 compile.

use anyhow::Result;
use std::path::Path;

/// Stub that Task 8 replaces with real manifest generation.
pub fn write_for_scenario(_expected_dir: &Path, _kid_dll: &Path) -> Result<()> {
    Ok(())
}
