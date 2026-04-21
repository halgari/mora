//! Version packing for plugins and Skyrim runtime.
//!
//! Matches `REL::Version` in CommonLibSSE-NG:
//! packed = (major & 0xFF) << 24 | (minor & 0xFF) << 16 | (patch & 0xFFF) << 4 | (build & 0xF)

/// A plugin's own version — embedded in `SKSEPlugin_Version` data.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PluginVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u16, // 12-bit-effective, asserted <= 0xFFF in `pack`
    pub build: u8,  // 4-bit-effective, asserted <= 0xF in `pack`
}

impl PluginVersion {
    /// Pack this version into the 32-bit layout expected by SKSE.
    pub const fn pack(self) -> u32 {
        assert!(self.patch <= 0xFFF, "patch exceeds 12 bits");
        assert!(self.build <= 0xF, "build exceeds 4 bits");
        ((self.major as u32) << 24)
            | ((self.minor as u32) << 16)
            | ((self.patch as u32) << 4)
            | (self.build as u32)
    }
}

/// A Skyrim runtime version — used for `compatible_versions` matching.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u16,
    pub build: u8,
}

impl RuntimeVersion {
    /// Same layout as `PluginVersion::pack`.
    pub const fn pack(self) -> u32 {
        assert!(self.patch <= 0xFFF, "patch exceeds 12 bits");
        assert!(self.build <= 0xF, "build exceeds 4 bits");
        ((self.major as u32) << 24)
            | ((self.minor as u32) << 16)
            | ((self.patch as u32) << 4)
            | (self.build as u32)
    }

    /// Well-known runtime: Skyrim SE 1.6.1170 (current AE).
    pub const SE_1_6_1170: RuntimeVersion = RuntimeVersion {
        major: 1,
        minor: 6,
        patch: 1170,
        build: 0,
    };
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plugin_version_packs_to_expected_bits() {
        // Mora v0.1.0 → (0,1,0,0) → 0x0001_0000
        let v = PluginVersion {
            major: 0,
            minor: 1,
            patch: 0,
            build: 0,
        };
        assert_eq!(v.pack(), 0x0001_0000);
    }

    #[test]
    fn runtime_version_ae_1_6_1170_packs_to_expected_bits() {
        // 1.6.1170.0 → (1,6,1170,0) → 0x0106_4920
        // Verify: 1170 = 0x492, shifted left 4 = 0x4920. OR with 1<<24 = 0x01000000,
        //         6<<16 = 0x00060000 → 0x01064920.
        assert_eq!(RuntimeVersion::SE_1_6_1170.pack(), 0x0106_4920);
    }

    #[test]
    fn plugin_version_max_field_values_pack_correctly() {
        let v = PluginVersion {
            major: 0xFF,
            minor: 0xFF,
            patch: 0xFFF,
            build: 0xF,
        };
        assert_eq!(v.pack(), 0xFFFF_FFFF);
    }

    #[test]
    #[should_panic(expected = "patch exceeds 12 bits")]
    fn plugin_version_rejects_overflowing_patch() {
        let v = PluginVersion {
            major: 0,
            minor: 0,
            patch: 0x1000, // one too many
            build: 0,
        };
        let _ = v.pack();
    }

    #[test]
    #[should_panic(expected = "build exceeds 4 bits")]
    fn plugin_version_rejects_overflowing_build() {
        let v = PluginVersion {
            major: 0,
            minor: 0,
            patch: 0,
            build: 0x10,
        };
        let _ = v.pack();
    }
}
