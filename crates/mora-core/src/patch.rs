//! `Patch` enum + `PatchFile` on-disk format.
//!
//! See `docs/src/mora-core-reference.md` for the postcard encoding and
//! enum-variant serialization stability story.

use serde::{Deserialize, Serialize};

use crate::form_id::FormId;

/// Fixed byte sequence at the start of every `PatchFile`.
pub const PATCH_FILE_MAGIC: [u8; 4] = *b"MORA";

/// On-disk format version. Bumped on incompatible changes to the
/// binary layout or enum-variant ordering.
pub const PATCH_FILE_VERSION: u32 = 1;

/// A single patch applied by the runtime at `kDataLoaded`.
///
/// **Variants must not be reordered or removed** — postcard
/// serializes enums by declaration order, so reordering breaks
/// existing patch files.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum Patch {
    /// Add `keyword` to the `BGSKeywordForm` sub-object of the form at
    /// `target`. Matches KID's core operation.
    AddKeyword { target: FormId, keyword: FormId },
}

impl Patch {
    /// A stable discriminant used for `PatchSink` sorting. Derived from
    /// the variant's position in the enum.
    pub fn opcode_tag(&self) -> u8 {
        match self {
            Patch::AddKeyword { .. } => 0,
        }
    }

    /// The target FormID this patch operates on. Used for `PatchSink`
    /// sorting inside a given opcode.
    pub fn target(&self) -> FormId {
        match self {
            Patch::AddKeyword { target, .. } => *target,
        }
    }
}

/// The full on-disk `mora_patches.bin` structure.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PatchFile {
    pub magic: [u8; 4],
    pub version: u32,
    pub load_order_hash: u64,
    pub patches: Vec<Patch>,
}

/// Errors encountered during `PatchFile` serialization or validation.
#[derive(Debug, thiserror::Error)]
pub enum PatchFileError {
    #[error("postcard serialize error: {0}")]
    Serialize(String),
    #[error("postcard deserialize error: {0}")]
    Deserialize(String),
    #[error("bad magic: got {0:?}, expected MORA")]
    BadMagic([u8; 4]),
    #[error("unsupported version: got {got}, known up to {known}")]
    UnsupportedVersion { got: u32, known: u32 },
}

impl PatchFile {
    pub fn new(load_order_hash: u64) -> Self {
        PatchFile {
            magic: PATCH_FILE_MAGIC,
            version: PATCH_FILE_VERSION,
            load_order_hash,
            patches: Vec::new(),
        }
    }

    /// Serialize to a `Vec<u8>` via postcard.
    pub fn to_bytes(&self) -> Result<Vec<u8>, PatchFileError> {
        postcard::to_stdvec(self).map_err(|e| PatchFileError::Serialize(e.to_string()))
    }

    /// Deserialize + validate from bytes.
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, PatchFileError> {
        let file: PatchFile =
            postcard::from_bytes(bytes).map_err(|e| PatchFileError::Deserialize(e.to_string()))?;
        if file.magic != PATCH_FILE_MAGIC {
            return Err(PatchFileError::BadMagic(file.magic));
        }
        if file.version > PATCH_FILE_VERSION {
            return Err(PatchFileError::UnsupportedVersion {
                got: file.version,
                known: PATCH_FILE_VERSION,
            });
        }
        Ok(file)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn magic_is_mora() {
        assert_eq!(&PATCH_FILE_MAGIC, b"MORA");
    }

    #[test]
    fn version_is_1() {
        assert_eq!(PATCH_FILE_VERSION, 1);
    }

    #[test]
    fn add_keyword_opcode_tag_is_0() {
        let p = Patch::AddKeyword {
            target: FormId(0x12EB7),
            keyword: FormId(0x1E718),
        };
        assert_eq!(p.opcode_tag(), 0);
        assert_eq!(p.target(), FormId(0x12EB7));
    }
}
