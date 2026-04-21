//! Xoshiro256** PRNG. One-shot — Mora needs a single `next_u64` per
//! chance roll, so we don't track state across rolls.
//!
//! Reference: https://prng.di.unimi.it/xoshiro256starstar.c
//!
//! ```text
//! result = rotl(s[1] * 5, 7) * 9
//! t = s[1] << 17
//! s[2] ^= s[0]
//! s[3] ^= s[1]
//! s[1] ^= s[2]
//! s[0] ^= s[3]
//! s[2] ^= t
//! s[3] = rotl(s[3], 45)
//! ```

/// A single Xoshiro256** step — consumes `state` in place, returns
/// the next 64-bit output.
pub fn xoshiro_next_u64(state: &mut [u64; 4]) -> u64 {
    let result = state[1].wrapping_mul(5).rotate_left(7).wrapping_mul(9);
    let t = state[1] << 17;
    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];
    state[2] ^= t;
    state[3] = state[3].rotate_left(45);
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::chance::splitmix::splitmix64_state;

    /// Reference: Xoshiro256** with the canonical SplitMix64 seeding of
    /// 0, first u64 output. Cross-checked against the reference C
    /// implementation at https://prng.di.unimi.it/xoshiro256starstar.c
    /// using the same SplitMix64 seeding convention.
    ///
    /// Expected: 0x99EC5F36CB75F2B4
    #[test]
    fn first_output_seed_0() {
        let mut state = splitmix64_state(0);
        let v = xoshiro_next_u64(&mut state);
        assert_eq!(v, 0x99EC_5F36_CB75_F2B4);
    }

    #[test]
    fn deterministic_given_state() {
        let mut a = splitmix64_state(42);
        let mut b = splitmix64_state(42);
        assert_eq!(xoshiro_next_u64(&mut a), xoshiro_next_u64(&mut b));
    }

    #[test]
    fn different_states_yield_different_outputs() {
        let mut a = splitmix64_state(1);
        let mut b = splitmix64_state(2);
        assert_ne!(xoshiro_next_u64(&mut a), xoshiro_next_u64(&mut b));
    }
}
