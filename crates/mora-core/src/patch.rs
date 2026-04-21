//! Stub. Populated in Task 4.

use serde::{Deserialize, Serialize};

use crate::form_id::FormId;

/// Placeholder — real impl in Task 4.
pub const PATCH_FILE_MAGIC: [u8; 4] = *b"MORA";
/// Placeholder — real impl in Task 4.
pub const PATCH_FILE_VERSION: u32 = 1;

/// Placeholder — real impl in Task 4.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub enum Patch {
    AddKeyword { target: FormId, keyword: FormId },
}

/// Placeholder — real impl in Task 4.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct PatchFile {
    pub magic: [u8; 4],
    pub version: u32,
    pub load_order_hash: u64,
    pub patches: Vec<Patch>,
}
