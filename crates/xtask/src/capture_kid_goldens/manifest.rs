//! Manifest generation: SHA256 over every ESP/ESM/ESL in the active
//! Skyrim data dir + KID version + Skyrim version, serialized to
//! `tests/golden-data/expected/<scenario>/manifest.json`.

use anyhow::{Context, Result};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::io::Read;
use std::path::Path;

/// SHA256 a file, lowercase hex. Chunked so we don't hold large ESPs
/// in memory.
pub fn hash_file_sha256(path: &Path) -> Result<String> {
    let f = std::fs::File::open(path).with_context(|| format!("opening {}", path.display()))?;
    let mut reader = std::io::BufReader::new(f);
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 64 * 1024];
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

/// Best-effort read of a Windows PE FileVersion resource. Returns None
/// if the file isn't parseable or has no VERSIONINFO. This is a
/// convenience — on failure we fall back to the SHA256 of KID.dll as
/// the identifier.
pub fn read_peek_kid_version(kid_dll: &Path) -> Option<String> {
    // Full PE version resource parsing is overkill for a manifest tag.
    // Use the SHA256 suffix — stable, collision-proof, trivially re-derivable.
    let h = hash_file_sha256(kid_dll).ok()?;
    Some(format!("sha256:{}", &h[..16]))
}

/// Hash every `.esp`, `.esm`, `.esl` in `data_dir` (top-level only —
/// loose BSAs and sub-dirs are intentionally ignored). Returns a
/// BTreeMap so iteration order is deterministic for stable JSON.
pub fn hash_data_dir(data_dir: &Path) -> Result<BTreeMap<String, String>> {
    let mut out = BTreeMap::new();
    if !data_dir.is_dir() {
        // Not an error — allows write_for_scenario to be called in
        // test contexts where the data dir is a placeholder.
        return Ok(out);
    }
    for entry in std::fs::read_dir(data_dir)? {
        let entry = entry?;
        if !entry.file_type()?.is_file() {
            continue;
        }
        let Some(name) = entry.file_name().to_str().map(str::to_string) else {
            continue;
        };
        let lower = name.to_ascii_lowercase();
        if !(lower.ends_with(".esp") || lower.ends_with(".esm") || lower.ends_with(".esl")) {
            continue;
        }
        let h = hash_file_sha256(&entry.path())?;
        out.insert(name, h);
    }
    Ok(out)
}

pub fn write_for_scenario(expected_dir: &Path, kid_dll: &Path) -> Result<()> {
    let data_dir = std::env::var("MORA_SKYRIM_DATA")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|_| std::path::PathBuf::from("/skyrim-base/Data"));

    let esp_hashes = hash_data_dir(&data_dir)?;
    let kid_version = read_peek_kid_version(kid_dll).unwrap_or_else(|| "unknown".to_string());
    // Skyrim version: placeholder. Bethesda doesn't expose a cheap
    // version byte for SkyrimSE.exe from the runner context; we record
    // the Skyrim.esm hash as the stable identifier instead (it's
    // already in esp_hashes).
    let skyrim_version = esp_hashes
        .get("Skyrim.esm")
        .cloned()
        .unwrap_or_else(|| "unknown".to_string());

    let captured_at = now_iso8601();

    let manifest = serde_json::json!({
        "captured_at": captured_at,
        "kid_version": kid_version,
        "skyrim_version": format!("sha256:{}", &skyrim_version.chars().take(16).collect::<String>()),
        "esp_hashes": esp_hashes,
    });

    let path = expected_dir.join("manifest.json");
    let pretty = serde_json::to_string_pretty(&manifest)?;
    std::fs::write(&path, pretty + "\n").with_context(|| format!("writing {}", path.display()))?;
    Ok(())
}

fn now_iso8601() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    // Very rough Y-M-DTHH:MM:SSZ. Exact-second precision is enough for
    // capture timestamps; we don't use this for correctness.
    let (year, month, day, h, m, s) = epoch_to_ymdhms(secs);
    format!("{year:04}-{month:02}-{day:02}T{h:02}:{m:02}:{s:02}Z")
}

/// Minimal UTC epoch-to-calendar conversion — avoids pulling in `chrono`
/// for one manifest field. Days computed via Howard Hinnant's
/// civil_from_days; sufficient for Skyrim pivot lifespan.
fn epoch_to_ymdhms(secs: u64) -> (u32, u32, u32, u32, u32, u32) {
    let s = (secs % 60) as u32;
    let m = ((secs / 60) % 60) as u32;
    let h = ((secs / 3600) % 24) as u32;
    let days = (secs / 86_400) as i64;
    // civil_from_days (Hinnant).
    let z = days + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = (z - era * 146_097) as u32;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m_calendar = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = y + if m_calendar <= 2 { 1 } else { 0 };
    (y as u32, m_calendar, d, h, m, s)
}
