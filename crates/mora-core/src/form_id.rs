//! Stub. Populated in Task 3.

use serde::Serialize;

/// Placeholder — real impl in Task 3.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
pub struct FormId(pub u32);

/// Placeholder — real impl in Task 3.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct FullFormId {
    pub plugin: String,
    pub local_id: u32,
}
