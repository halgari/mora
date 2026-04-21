//! Reference — a KID-style form / plugin / editor-ID reference.
//!
//! KID's `get_record_type` dispatches on the content of the string:
//!   - contains `~` → `FormIdWithPlugin`
//!   - contains `.es` → `PluginName`
//!   - only hex chars (optional `0x` prefix) → `FormIdOnly`
//!   - else → `EditorId`

use mora_core::FormId;
use mora_esp::EspWorld;

/// A KID reference.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Reference {
    /// Editor-ID string, e.g. `"WeapMaterialIron"`.
    EditorId(String),
    /// FormID (3-byte local) paired with the defining plugin name,
    /// e.g. `"0x01E718~Skyrim.esm"` or `"0x1E718~Skyrim.esm"`.
    FormIdWithPlugin {
        local_id: u32,
        plugin: String,
    },
    /// A plugin filename (no form), used as a file filter.
    PluginName(String),
    /// A bare hex FormID with no plugin qualifier. Load-order-sensitive.
    FormIdOnly(u32),
}

impl Reference {
    /// Parse a single KID reference string.
    pub fn parse(s: &str) -> Self {
        if let Some(tilde) = s.find('~') {
            let (hex, plugin) = s.split_at(tilde);
            let plugin = &plugin[1..]; // strip '~'
            if let Some(local) = parse_hex(hex) {
                return Reference::FormIdWithPlugin {
                    local_id: local,
                    plugin: plugin.to_string(),
                };
            }
        }
        if is_mod_name(s) {
            return Reference::PluginName(s.to_string());
        }
        if let Some(raw) = parse_hex(s) {
            return Reference::FormIdOnly(raw);
        }
        Reference::EditorId(s.to_string())
    }

    /// Resolve this reference to a FormId using the loaded world.
    /// Returns None if the reference cannot be resolved (missing
    /// plugin, unknown editor-ID, etc.).
    ///
    /// Note: `PluginName` references are NOT forms — they're plugin
    /// filters. `resolve_form` returns None for them. Callers
    /// distinguish with `matches!(r, Reference::PluginName(_))` when
    /// they need plugin filter semantics.
    pub fn resolve_form(&self, world: &EspWorld) -> Option<FormId> {
        match self {
            Reference::EditorId(edid) => world.resolve_keyword_by_editor_id(edid),
            Reference::FormIdWithPlugin { local_id, plugin } => {
                // Find the plugin's slot in the load order.
                let slot = world.load_order.lookup(plugin)?;
                Some(FormId(slot.compose_form_id(*local_id & 0x00FF_FFFF)))
            }
            Reference::PluginName(_) => None,
            Reference::FormIdOnly(raw) => Some(FormId(*raw)),
        }
    }
}

fn parse_hex(s: &str) -> Option<u32> {
    let s = s.trim();
    let stripped = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")).unwrap_or(s);
    if stripped.is_empty() || !stripped.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    u32::from_str_radix(stripped, 16).ok()
}

fn is_mod_name(s: &str) -> bool {
    s.to_ascii_lowercase().contains(".es")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_editor_id() {
        assert_eq!(
            Reference::parse("WeapMaterialIron"),
            Reference::EditorId("WeapMaterialIron".into())
        );
    }

    #[test]
    fn parses_form_id_with_plugin() {
        assert_eq!(
            Reference::parse("0x1E718~Skyrim.esm"),
            Reference::FormIdWithPlugin {
                local_id: 0x1E718,
                plugin: "Skyrim.esm".into()
            }
        );
    }

    #[test]
    fn parses_plugin_name() {
        assert_eq!(
            Reference::parse("Skyrim.esm"),
            Reference::PluginName("Skyrim.esm".into())
        );
        assert_eq!(
            Reference::parse("MyMod.esp"),
            Reference::PluginName("MyMod.esp".into())
        );
        assert_eq!(
            Reference::parse("LightMod.esl"),
            Reference::PluginName("LightMod.esl".into())
        );
    }

    #[test]
    fn parses_bare_hex() {
        assert_eq!(Reference::parse("0x1E718"), Reference::FormIdOnly(0x1E718));
        // No prefix, just hex:
        assert_eq!(Reference::parse("1E718"), Reference::FormIdOnly(0x1E718));
    }

    #[test]
    fn editor_id_with_digits_is_still_editor_id() {
        // Editor IDs often contain digits; what matters is that they
        // don't consist ONLY of hex chars (which would parse as FormID).
        // "WeapMaterialIron01" is not all hex because W/M/L/R/N/S
        // aren't in [0-9a-fA-F].
        assert_eq!(
            Reference::parse("WeapMaterialIron01"),
            Reference::EditorId("WeapMaterialIron01".into())
        );
    }

    #[test]
    fn all_hex_editor_id_parses_as_form_id() {
        // A quirk matching KID: "DEADBEEF" is all-hex → parsed as FormID
        // even though the user might have meant it as an editor-ID.
        // KID has this same behavior (see `is_only_hex` in CLIBUtil).
        assert_eq!(
            Reference::parse("DEADBEEF"),
            Reference::FormIdOnly(0xDEADBEEF)
        );
    }
}
