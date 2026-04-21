//! Stub. Populated in Task 8.

/// Placeholder — real impl in Task 8.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RecordType {
    Weapon,
    Armor,
    Other(String),
}

/// Placeholder — real impl in Task 8.
#[derive(Debug, Clone)]
pub struct KidRule {
    pub keyword: crate::reference::Reference,
    pub record_type: RecordType,
}
