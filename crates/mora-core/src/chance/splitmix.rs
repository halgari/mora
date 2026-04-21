//! SplitMix64 state initializer, used to seed the Xoshiro256** state
//! from a single `u64` seed. Matches XoshiroCpp's `Xoshiro256StarStar::Xoshiro256StarStar(u64)`.

/// One step of SplitMix64. Modifies `seed` in place, returns the
/// next mixed output.
///
/// ```text
/// seed  = seed + 0x9E37_79B9_7F4A_7C15
/// z     = seed
/// z     = (z ^ (z >> 30)) * 0xBF58_476D_1CE4_E5B9
/// z     = (z ^ (z >> 27)) * 0x94D0_49BB_1331_11EB
/// z     = z ^ (z >> 31)
/// return z
/// ```
pub const fn splitmix64_next(seed: &mut u64) -> u64 {
    *seed = seed.wrapping_add(0x9E37_79B9_7F4A_7C15);
    let mut z = *seed;
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

/// Seed a 4-word Xoshiro state from a single `u64` via 4 SplitMix64 steps.
pub const fn splitmix64_state(seed: u64) -> [u64; 4] {
    let mut s = seed;
    let s0 = splitmix64_next(&mut s);
    let s1 = splitmix64_next(&mut s);
    let s2 = splitmix64_next(&mut s);
    let s3 = splitmix64_next(&mut s);
    [s0, s1, s2, s3]
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Reference values computed from the canonical SplitMix64 algorithm
    /// (https://prng.di.unimi.it/splitmix64.c). Seed = 0, first 4 outputs:
    ///   0xE220A8397B1DCDAF
    ///   0x6E789E6AA1B965F4
    ///   0x06C45D188009454F
    ///   0xF88BB8A8724C81EC
    #[test]
    fn first_four_outputs_seed_0() {
        let state = splitmix64_state(0);
        assert_eq!(state[0], 0xE220_A839_7B1D_CDAF);
        assert_eq!(state[1], 0x6E78_9E6A_A1B9_65F4);
        assert_eq!(state[2], 0x06C4_5D18_8009_454F);
        assert_eq!(state[3], 0xF88B_B8A8_724C_81EC);
    }

    /// Deterministic — same input → same output.
    #[test]
    fn deterministic_given_seed() {
        let a = splitmix64_state(0xDEAD_BEEF_CAFE_F00D);
        let b = splitmix64_state(0xDEAD_BEEF_CAFE_F00D);
        assert_eq!(a, b);
    }

    #[test]
    fn different_seeds_produce_different_states() {
        let a = splitmix64_state(1);
        let b = splitmix64_state(2);
        assert_ne!(a, b);
    }
}
