//! MSVC `std::uniform_real_distribution<float>` port for range `[0, 100)`.
//!
//! Matches the specific algorithm MSVC STL uses on Windows x64 with a
//! 64-bit uniform RNG:
//!   1. Convert raw u64 to f64 by dividing by 2^64.
//!   2. Narrow to f32 (IEEE 754 round-to-nearest, matching MSVC
//!      default rounding mode).
//!   3. Out-of-range clamp: if result >= 1.0, snap to 0.0 (MSVC's
//!      `generate_canonical` guard).
//!   4. Scale by the range (100.0 for chance).
//!
//! This is the exact same step sequence KID applies — see
//! `docs/src/mora-core-reference.md`.

/// Draw a single float in `[0, 100)` from a raw 64-bit engine output.
pub fn draw_percent_from_u64(raw: u64) -> f32 {
    // Step 1 + 2: raw / 2^64 in f64, narrow to f32.
    // 2^64 = 18446744073709551616.0
    let scale_f64: f64 = (raw as f64) / 18446744073709551616.0_f64;
    let mut clamp_f32: f32 = scale_f64 as f32;

    // Step 3: MSVC's out-of-range snap. Rare (requires raw ~= 2^64) but
    // well-defined per the STL spec.
    if clamp_f32 >= 1.0_f32 {
        clamp_f32 = 0.0_f32;
    }

    // Step 4: scale to [0, 100).
    100.0_f32 * clamp_f32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn raw_0_yields_0_0() {
        assert_eq!(draw_percent_from_u64(0), 0.0_f32);
    }

    #[test]
    fn raw_u64_max_snapped_to_0_0() {
        // raw = 2^64 - 1 → f64 scale ~= 0.99999…9; narrowed to f32 becomes 1.0f.
        // MSVC's snap kicks in and returns 0.0, percent = 0.
        let v = draw_percent_from_u64(u64::MAX);
        assert_eq!(v, 0.0_f32);
    }

    #[test]
    fn raw_half_yields_50_0() {
        // raw = 2^63 → scale_f64 = 0.5 exactly → clamp_f32 = 0.5 → percent = 50.0.
        let v = draw_percent_from_u64(1u64 << 63);
        assert_eq!(v, 50.0_f32);
    }

    #[test]
    fn output_always_in_0_100_range() {
        // A few random-ish raws.
        for &raw in &[0u64, 1, 42, 0xDEAD_BEEF, 0xCAFE_BABE, u64::MAX] {
            let v = draw_percent_from_u64(raw);
            assert!((0.0..100.0).contains(&v), "raw {raw:x} produced {v}");
        }
    }
}
