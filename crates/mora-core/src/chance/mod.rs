//! KID-bit-compatible deterministic chance RNG.
//!
//! See `docs/src/mora-core-reference.md` for the algorithm.

pub mod fnv;
pub mod msvc_uniform;
pub mod splitmix;
pub mod szudzik;
pub mod xoshiro;

use crate::form_id::FormId;

/// KID-compatible chance resolver. Zero state — a single instance is
/// safe to share across all frontends + threads.
#[derive(Debug, Clone, Copy, Default)]
pub struct DeterministicChance;

impl DeterministicChance {
    /// Construct a chance resolver configured to match KID exactly.
    /// Reserved for a future constructor if we add variants (e.g.
    /// SPID-compatible or user-custom); Plan 4 only has one flavor.
    pub const fn kid_compatible() -> Self {
        DeterministicChance
    }

    /// The full KID roll: does this `(keyword, form_id)` pair pass a
    /// rule with the given `chance` percentage?
    ///
    /// `chance` is 0..=100 inclusive; values ≥ 100 always pass, values
    /// ≤ 0 always fail (matching KID's trivial paths which skip the
    /// roll entirely).
    pub fn passes(&self, keyword_editor_id: &str, form_id: FormId, chance: u8) -> bool {
        if chance >= 100 {
            return true;
        }
        if chance == 0 {
            return false;
        }
        let percent = self.roll_percent(keyword_editor_id, form_id);
        percent <= chance as f32
    }

    /// The raw 0..100 percentage rolled for this pair. Exposed for
    /// diagnostics and for M4's golden-test harness.
    pub fn roll_percent(&self, keyword_editor_id: &str, form_id: FormId) -> f32 {
        let kw_hash = fnv::fnv1a_32(keyword_editor_id.as_bytes());
        let seed = szudzik::szudzik_pair(kw_hash as u64, form_id.raw() as u64);
        let mut state = splitmix::splitmix64_state(seed);
        let raw = xoshiro::xoshiro_next_u64(&mut state);
        msvc_uniform::draw_percent_from_u64(raw)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chance_100_always_passes() {
        let c = DeterministicChance::kid_compatible();
        for &fid in &[0u32, 0x12EB7, 0xFFFF_FFFF] {
            assert!(c.passes("AnyKeyword", FormId(fid), 100));
        }
    }

    #[test]
    fn chance_0_always_fails() {
        let c = DeterministicChance::kid_compatible();
        for &fid in &[0u32, 0x12EB7, 0xFFFF_FFFF] {
            assert!(!c.passes("AnyKeyword", FormId(fid), 0));
        }
    }

    #[test]
    fn deterministic_given_inputs() {
        let c = DeterministicChance::kid_compatible();
        let a = c.roll_percent("WeapMaterialIron", FormId(0x12EB7));
        let b = c.roll_percent("WeapMaterialIron", FormId(0x12EB7));
        assert_eq!(a, b);
    }

    #[test]
    fn different_keywords_produce_different_rolls() {
        let c = DeterministicChance::kid_compatible();
        let a = c.roll_percent("Alpha", FormId(0x12EB7));
        let b = c.roll_percent("Beta", FormId(0x12EB7));
        assert_ne!(a, b);
    }

    #[test]
    fn roll_percent_is_in_range() {
        let c = DeterministicChance::kid_compatible();
        for fid in 0u32..1000 {
            let v = c.roll_percent("TestKeyword", FormId(fid));
            assert!((0.0..100.0).contains(&v), "fid {fid} produced {v}");
        }
    }

    /// Sanity check: at chance=50, a few thousand rolls should
    /// converge around ~50% pass rate. Tight bound: 47-53%.
    #[test]
    fn distribution_50_percent_converges() {
        let c = DeterministicChance::kid_compatible();
        let n = 10_000u32;
        let passes = (0..n)
            .filter(|i| c.passes("SanityKeyword", FormId(*i), 50))
            .count();
        let pct = (passes as f64 / n as f64) * 100.0;
        assert!(
            (47.0..53.0).contains(&pct),
            "chance=50 over {n} rolls passed {pct}%, expected ~50"
        );
    }
}
