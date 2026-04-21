//! Load-order hashing for `mora_patches.bin` sanity check.
//!
//! The hash is a blake3 digest truncated to 64 bits over a
//! canonical representation of the active load order. The runtime
//! recomputes it and refuses to apply the patch file if it differs —
//! catches "user regenerated their load order without recompiling".

use crate::plugin::EspPlugin;

/// Compute a canonical 64-bit digest over the given plugin list.
///
/// Canonical form: for each plugin, append:
///   `<filename_lowercase>\0<esm_flag>\0<esl_flag>\0<master_count>\0<master_filenames_lowercase_nul_joined>\0`
///
/// The master list is part of the hash because a plugin updated with
/// new or reordered masters produces new runtime FormIDs even if the
/// plugin filename is unchanged.
pub fn compute_load_order_hash(plugins: &[EspPlugin]) -> u64 {
    let mut hasher = blake3::Hasher::new();
    for p in plugins {
        hasher.update(p.filename.to_ascii_lowercase().as_bytes());
        hasher.update(&[0]);
        hasher.update(&[p.is_esm() as u8]);
        hasher.update(&[0]);
        hasher.update(&[p.is_esl() as u8]);
        hasher.update(&[0]);
        hasher.update(&(p.header.masters.len() as u32).to_le_bytes());
        hasher.update(&[0]);
        for master in &p.header.masters {
            hasher.update(master.to_ascii_lowercase().as_bytes());
            hasher.update(&[0]);
        }
    }
    let digest = hasher.finalize();
    // Truncate to 64 bits (first 8 bytes).
    let bytes: [u8; 32] = digest.into();
    u64::from_le_bytes(bytes[..8].try_into().unwrap())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_plugin_list_hashes_consistently() {
        let a = compute_load_order_hash(&[]);
        let b = compute_load_order_hash(&[]);
        assert_eq!(a, b);
    }
}
