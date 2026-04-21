//! Integration tests for `skse-rs` ABI struct layouts.
//!
//! If any of these fail, the SKSE plugin DLL will fail to load
//! silently — SKSE rejects plugins with a `SKSEPlugin_Version`
//! whose layout doesn't match the expected size.

use skse_rs::ffi::*;

#[test]
fn plugin_version_data_is_0x350_bytes() {
    assert_eq!(std::mem::size_of::<PluginVersionData>(), 0x350);
}

#[test]
fn plugin_version_data_field_offsets_match_spec() {
    // Offsets from docs/src/skse-rs-ffi-reference.md
    assert_eq!(std::mem::offset_of!(PluginVersionData, data_version), 0x000);
    assert_eq!(std::mem::offset_of!(PluginVersionData, plugin_version), 0x004);
    assert_eq!(std::mem::offset_of!(PluginVersionData, plugin_name), 0x008);
    assert_eq!(std::mem::offset_of!(PluginVersionData, author), 0x108);
    assert_eq!(std::mem::offset_of!(PluginVersionData, support_email), 0x208);
    assert_eq!(std::mem::offset_of!(PluginVersionData, flags_a), 0x304);
    assert_eq!(std::mem::offset_of!(PluginVersionData, flags_b), 0x308);
    assert_eq!(
        std::mem::offset_of!(PluginVersionData, compatible_versions),
        0x30C
    );
    assert_eq!(std::mem::offset_of!(PluginVersionData, xse_minimum), 0x34C);
}

#[test]
fn plugin_info_is_0x18_bytes() {
    assert_eq!(std::mem::size_of::<PluginInfo>(), 0x18);
}

#[test]
fn skse_interface_is_0x30_bytes() {
    assert_eq!(std::mem::size_of::<SKSEInterface>(), 0x30);
}

#[test]
fn skse_messaging_interface_is_0x20_bytes() {
    assert_eq!(std::mem::size_of::<SKSEMessagingInterface>(), 0x20);
}

#[test]
fn message_type_k_data_loaded_is_8() {
    assert_eq!(MessageType::DataLoaded as u32, 8);
}

#[test]
fn kmessaging_is_5() {
    assert_eq!(KMESSAGING, 5);
}
