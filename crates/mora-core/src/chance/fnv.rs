//! FNV-1a-32 hash — ported to match KID's `clib_util::hash::fnv1a_32`
//! exactly.
//!
//! Standard algorithm:
//!   h = 0x811c9dc5 (offset basis)
//!   for each byte b: h = (h ^ b) * 0x01000193 (prime)
//!
//! Operations are `u32` with wrapping multiplication to match C++
//! unsigned overflow semantics.

/// FNV-1a-32 offset basis.
const OFFSET_BASIS: u32 = 0x811c_9dc5;
/// FNV-1a-32 prime.
const PRIME: u32 = 0x0100_0193;

/// Hash `bytes` with FNV-1a-32. Deterministic, case-sensitive.
pub fn fnv1a_32(bytes: &[u8]) -> u32 {
    let mut h = OFFSET_BASIS;
    for &b in bytes {
        h ^= b as u32;
        h = h.wrapping_mul(PRIME);
    }
    h
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Empty input → offset basis.
    #[test]
    fn empty_returns_offset_basis() {
        assert_eq!(fnv1a_32(b""), 0x811c_9dc5);
    }

    /// Reference value for "a" (a single byte 0x61):
    ///   h = 0x811c9dc5 ^ 0x61 = 0x811c9da4
    ///   h * 0x01000193 (wrapping) = 0xE40C292C
    #[test]
    fn single_char_a() {
        assert_eq!(fnv1a_32(b"a"), 0xE40C_292C);
    }

    /// Reference value for "foobar" (well-known FNV-1a-32 test vector):
    ///   0xBF9CF968
    #[test]
    fn known_vector_foobar() {
        assert_eq!(fnv1a_32(b"foobar"), 0xBF9C_F968);
    }

    /// Deterministic — same input always same output.
    #[test]
    fn deterministic() {
        let a = fnv1a_32(b"WeapMaterialIron");
        let b = fnv1a_32(b"WeapMaterialIron");
        assert_eq!(a, b);
    }

    /// Different inputs → different outputs (avalanche sanity).
    #[test]
    fn distinct_outputs() {
        assert_ne!(fnv1a_32(b"Alpha"), fnv1a_32(b"Beta"));
        assert_ne!(fnv1a_32(b"a"), fnv1a_32(b"b"));
    }
}
