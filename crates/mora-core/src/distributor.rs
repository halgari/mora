//! The `Distributor` trait — the extensibility hinge for future
//! frontends (mora-kid, mora-spid, mora-skypatcher, …).
//!
//! Plan 4 uses a placeholder [`EspWorld`] marker; Plan 5 replaces it
//! with the real `mora_esp::EspWorld`.

use crate::chance::DeterministicChance;
use crate::patch_sink::PatchSink;

/// Placeholder for the real `mora_esp::EspWorld`. Plan 5 replaces
/// with the actual indexed ESP view; downstream trait users compile
/// against this marker until then.
pub struct EspWorld;

/// Per-run statistics summed across all registered frontends.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats {
    pub rules_evaluated: u64,
    pub candidates_considered: u64,
    pub patches_emitted: u64,
    pub rejected_by_chance: u64,
    pub rejected_by_filter: u64,
}

impl std::ops::AddAssign for DistributorStats {
    fn add_assign(&mut self, rhs: Self) {
        self.rules_evaluated += rhs.rules_evaluated;
        self.candidates_considered += rhs.candidates_considered;
        self.patches_emitted += rhs.patches_emitted;
        self.rejected_by_chance += rhs.rejected_by_chance;
        self.rejected_by_filter += rhs.rejected_by_filter;
    }
}

/// A distributor frontend — consumes ESP + rules → produces patches.
pub trait Distributor {
    /// Error type surfaced by [`lower`]. Must be `Send + Sync + 'static`.
    type Error: std::error::Error + Send + Sync + 'static;

    /// Short name for diagnostics (e.g. `"kid"`, `"spid"`).
    fn name(&self) -> &'static str;

    /// Produce patches from the loaded ESP world + chance RNG, pushing
    /// each into the provided sink.
    fn lower(
        &self,
        world: &EspWorld,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error>;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stats_add_assign() {
        let mut a = DistributorStats {
            rules_evaluated: 1,
            candidates_considered: 2,
            patches_emitted: 3,
            rejected_by_chance: 4,
            rejected_by_filter: 5,
        };
        let b = DistributorStats {
            rules_evaluated: 10,
            candidates_considered: 20,
            patches_emitted: 30,
            rejected_by_chance: 40,
            rejected_by_filter: 50,
        };
        a += b;
        assert_eq!(
            a,
            DistributorStats {
                rules_evaluated: 11,
                candidates_considered: 22,
                patches_emitted: 33,
                rejected_by_chance: 44,
                rejected_by_filter: 55,
            }
        );
    }
}
