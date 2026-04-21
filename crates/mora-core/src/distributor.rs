//! Stub. Populated in Task 6.

/// Placeholder — opaque marker so downstream compiles. Plan 5
/// replaces with real `mora_esp::EspWorld`.
pub struct EspWorld;

/// Placeholder — real impl in Task 6.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats;

/// Placeholder — real impl in Task 6.
pub trait Distributor {
    type Error;
    fn name(&self) -> &'static str;
}
