//! The `Distributor` trait — the extensibility hinge for future
//! frontends (mora-kid, mora-spid, mora-skypatcher, …).
//!
//! `Distributor` is generic over the world type so we avoid a
//! `mora-core → mora-esp → mora-core` cycle. Frontends bind the
//! generic to `mora_esp::EspWorld`; `mora-cli` ties everything
//! together.

use crate::chance::DeterministicChance;
use crate::patch_sink::PatchSink;

/// Per-run statistics summed across all registered frontends.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats {
    pub rules_evaluated: u64,
    pub candidates_considered: u64,
    pub patches_emitted: u64,
    pub rejected_by_chance: u64,
    pub rejected_by_filter: u64,
    pub rejected_by_exclusive_group: u64,
}

impl std::ops::AddAssign for DistributorStats {
    fn add_assign(&mut self, rhs: Self) {
        self.rules_evaluated += rhs.rules_evaluated;
        self.candidates_considered += rhs.candidates_considered;
        self.patches_emitted += rhs.patches_emitted;
        self.rejected_by_chance += rhs.rejected_by_chance;
        self.rejected_by_filter += rhs.rejected_by_filter;
        self.rejected_by_exclusive_group += rhs.rejected_by_exclusive_group;
    }
}

/// A distributor frontend — consumes ESP + rules → produces patches.
///
/// Generic over `World` so `mora-core` doesn't depend on `mora-esp`.
/// Frontends (mora-kid, etc.) instantiate with
/// `mora_esp::EspWorld` in their `Distributor` impl.
pub trait Distributor<World: ?Sized> {
    type Error: std::error::Error + Send + Sync + 'static;

    fn name(&self) -> &'static str;

    fn lower(
        &self,
        world: &World,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error>;
}

/// Placeholder kept for backward compatibility with Plan 4 code.
/// New code should use the real `mora_esp::EspWorld`.
#[deprecated(note = "use mora_esp::EspWorld directly")]
pub struct EspWorld;

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
            rejected_by_exclusive_group: 6,
        };
        let b = DistributorStats {
            rules_evaluated: 10,
            candidates_considered: 20,
            patches_emitted: 30,
            rejected_by_chance: 40,
            rejected_by_filter: 50,
            rejected_by_exclusive_group: 60,
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
                rejected_by_exclusive_group: 66,
            }
        );
    }
}
