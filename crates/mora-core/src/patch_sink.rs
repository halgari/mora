//! `PatchSink` — append-only patch collector with dedup and
//! stable sort on finalize.

use std::collections::HashSet;

use crate::patch::{PATCH_FILE_MAGIC, PATCH_FILE_VERSION, Patch, PatchFile};

/// Append-only patch collector. Frontends push into it; finalize
/// produces a sorted, deduped `PatchFile`.
#[derive(Default)]
pub struct PatchSink {
    patches: Vec<Patch>,
    seen: HashSet<Patch>,
    duplicates_skipped: u64,
    load_order_hash: u64,
}

impl PatchSink {
    pub fn new() -> Self {
        Self::default()
    }

    /// Sets the load-order hash that will be embedded in the finalized
    /// `PatchFile`. Caller is responsible for computing an appropriate
    /// value (typically a blake3 digest over plugin names + master
    /// references; see `mora-esp` Plan 5).
    pub fn set_load_order_hash(&mut self, hash: u64) {
        self.load_order_hash = hash;
    }

    /// Push a patch. Identical patches already in the sink are
    /// silently deduped; `duplicates_skipped` counter increments.
    pub fn push(&mut self, patch: Patch) {
        if self.seen.insert(patch.clone()) {
            self.patches.push(patch);
        } else {
            self.duplicates_skipped += 1;
        }
    }

    /// Number of unique patches currently in the sink.
    pub fn len(&self) -> usize {
        self.patches.len()
    }

    pub fn is_empty(&self) -> bool {
        self.patches.is_empty()
    }

    /// Count of duplicate pushes dropped.
    pub fn duplicates_skipped(&self) -> u64 {
        self.duplicates_skipped
    }

    /// Consume the sink into a sorted `PatchFile`. Patches are sorted
    /// by `(opcode_tag, target_form_id.raw())` for a stable, reproducible
    /// output across runs.
    pub fn finalize(mut self) -> PatchFile {
        self.patches
            .sort_by_key(|p| (p.opcode_tag(), p.target().raw()));
        PatchFile {
            magic: PATCH_FILE_MAGIC,
            version: PATCH_FILE_VERSION,
            load_order_hash: self.load_order_hash,
            patches: self.patches,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::form_id::FormId;

    fn add_kw(t: u32, k: u32) -> Patch {
        Patch::AddKeyword {
            target: FormId(t),
            keyword: FormId(k),
        }
    }

    #[test]
    fn push_dedupes_identical_patches() {
        let mut sink = PatchSink::new();
        sink.push(add_kw(1, 2));
        sink.push(add_kw(1, 2));
        sink.push(add_kw(1, 2));
        assert_eq!(sink.len(), 1);
        assert_eq!(sink.duplicates_skipped(), 2);
    }

    #[test]
    fn push_keeps_distinct_patches() {
        let mut sink = PatchSink::new();
        sink.push(add_kw(1, 2));
        sink.push(add_kw(1, 3));
        sink.push(add_kw(2, 2));
        assert_eq!(sink.len(), 3);
        assert_eq!(sink.duplicates_skipped(), 0);
    }

    #[test]
    fn finalize_sorts_by_target() {
        let mut sink = PatchSink::new();
        sink.push(add_kw(3, 1));
        sink.push(add_kw(1, 1));
        sink.push(add_kw(2, 1));
        let file = sink.finalize();
        let targets: Vec<u32> = file.patches.iter().map(|p| p.target().raw()).collect();
        assert_eq!(targets, vec![1, 2, 3]);
    }

    #[test]
    fn finalize_preserves_magic_version_and_hash() {
        let mut sink = PatchSink::new();
        sink.set_load_order_hash(0xCAFE_BABE_DEAD_BEEF);
        let file = sink.finalize();
        assert_eq!(&file.magic, b"MORA");
        assert_eq!(file.version, 1);
        assert_eq!(file.load_order_hash, 0xCAFE_BABE_DEAD_BEEF);
    }

    #[test]
    fn new_sink_is_empty() {
        let sink = PatchSink::new();
        assert!(sink.is_empty());
        assert_eq!(sink.len(), 0);
    }
}
