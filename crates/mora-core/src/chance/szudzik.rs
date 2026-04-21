//! Szudzik pairing function — maps two `u64`s to a single `u64`.
//!
//! Matches `clib_util::hash::szudzik_pair` (used by KID's chance
//! seeding).
//!
//! ```text
//! szudzik(a, b) = if a >= b { a*a + a + b } else { b*b + a }
//! ```

/// Pair two `u64` values into one. Deterministic; matches KID.
///
/// Arithmetic uses wrapping multiplication to match C++ unsigned overflow
/// semantics (defined behavior on `uint64_t`).
pub const fn szudzik_pair(a: u64, b: u64) -> u64 {
    if a >= b {
        a.wrapping_mul(a).wrapping_add(a).wrapping_add(b)
    } else {
        b.wrapping_mul(b).wrapping_add(a)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn zero_zero() {
        assert_eq!(szudzik_pair(0, 0), 0);
    }

    #[test]
    fn a_greater_branch() {
        // a = 10, b = 3 → a*a + a + b = 100 + 10 + 3 = 113
        assert_eq!(szudzik_pair(10, 3), 113);
    }

    #[test]
    fn a_less_branch() {
        // a = 3, b = 10 → b*b + a = 100 + 3 = 103
        assert_eq!(szudzik_pair(3, 10), 103);
    }

    #[test]
    fn a_equal_b_uses_greater_branch() {
        // a = 5, b = 5 → a*a + a + b = 25 + 5 + 5 = 35
        assert_eq!(szudzik_pair(5, 5), 35);
    }

    #[test]
    fn asymmetric_in_general() {
        assert_ne!(szudzik_pair(3, 10), szudzik_pair(10, 3));
    }

    #[test]
    fn wrapping_large_values_dont_panic() {
        // 2^32 * 2^32 overflows u64 when summed with +a+b; just verify
        // the function doesn't panic and produces a deterministic result.
        let x = szudzik_pair(0xFFFF_FFFF, 0xFFFF_FFFF);
        let y = szudzik_pair(0xFFFF_FFFF, 0xFFFF_FFFF);
        assert_eq!(x, y); // determinism
    }
}
