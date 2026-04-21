//! FormID newtypes.
//!
//! Mora uses fully-resolved 32-bit FormIDs internally. Plugin-qualified
//! IDs (e.g. from KID INIs) are represented as `FullFormId` and
//! resolved against a load order by `mora-esp` (Plan 5).

use serde::{Deserialize, Serialize};

/// A fully-resolved 32-bit FormID (mod index baked into the high byte).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
pub struct FormId(pub u32);

impl FormId {
    /// The low 24 bits — the "local" form id.
    pub const fn local_id(self) -> u32 {
        self.0 & 0x00FF_FFFF
    }

    /// The high 8 bits — the mod index in the active load order.
    pub const fn mod_index(self) -> u8 {
        (self.0 >> 24) as u8
    }

    /// Construct from mod index + local id.
    pub const fn from_parts(mod_index: u8, local_id: u32) -> Self {
        debug_assert!(local_id & 0xFF00_0000 == 0, "local_id must fit in 24 bits");
        FormId(((mod_index as u32) << 24) | (local_id & 0x00FF_FFFF))
    }

    /// The raw 32-bit value.
    pub const fn raw(self) -> u32 {
        self.0
    }
}

impl std::fmt::Display for FormId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "0x{:08X}", self.0)
    }
}

impl From<u32> for FormId {
    fn from(raw: u32) -> Self {
        FormId(raw)
    }
}

/// A plugin-qualified, load-order-independent form reference.
///
/// Used when a KID INI says `0xABC~PluginName.esm` — the `plugin`
/// string + the local id. Resolved to a `FormId` against a live load
/// order by `mora-esp::EspWorld::resolve_full_form_id`.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct FullFormId {
    /// The plugin filename (e.g. `"Skyrim.esm"`). Case-insensitive for
    /// lookups; the canonical form stores the filename as-given.
    pub plugin: String,
    /// The local form id — low 24 bits. The high byte of the full
    /// FormID is determined at resolution time by the plugin's
    /// compile-time mod index.
    pub local_id: u32,
}

impl FullFormId {
    pub fn new(plugin: impl Into<String>, local_id: u32) -> Self {
        FullFormId {
            plugin: plugin.into(),
            local_id: local_id & 0x00FF_FFFF,
        }
    }
}

impl std::fmt::Display for FullFormId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "0x{:06X}~{}", self.local_id, self.plugin)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn form_id_parts() {
        let f = FormId(0x0A_12_34_56);
        assert_eq!(f.mod_index(), 0x0A);
        assert_eq!(f.local_id(), 0x12_34_56);
        assert_eq!(f.raw(), 0x0A_12_34_56);
    }

    #[test]
    fn form_id_from_parts() {
        let f = FormId::from_parts(0x0A, 0x12_34_56);
        assert_eq!(f, FormId(0x0A_12_34_56));
    }

    #[test]
    fn form_id_display_is_hex() {
        assert_eq!(format!("{}", FormId(0x0001_2EB7)), "0x00012EB7");
    }

    #[test]
    fn full_form_id_constructs_and_masks_high_byte() {
        // If caller passes a full 32-bit value, the high byte is stripped.
        let f = FullFormId::new("Skyrim.esm", 0xFF_12_34_56);
        assert_eq!(f.local_id, 0x12_34_56);
        assert_eq!(f.plugin, "Skyrim.esm");
    }

    #[test]
    fn full_form_id_display_format() {
        let f = FullFormId::new("Skyrim.esm", 0x12EB7);
        assert_eq!(format!("{}", f), "0x012EB7~Skyrim.esm");
    }
}
