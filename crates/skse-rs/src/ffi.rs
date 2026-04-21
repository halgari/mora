//! SKSE C-ABI types.
//!
//! These structs mirror the layouts in `CommonLibSSE-NG/include/SKSE/`
//! (see `docs/src/skse-rs-ffi-reference.md`). The types here are
//! unsafe to construct outside of their intended roles — they model
//! wire-format contracts, not safe Rust values.

use core::ffi::{c_char, c_void};

/// SKSE `kMessaging` interface ID — passed to `SKSEInterface::query_interface`.
pub const KMESSAGING: u32 = 5;

/// Interface version of `SKSEMessagingInterface` at AE 1.6.x.
pub const MESSAGING_INTERFACE_VERSION: u32 = 2;

/// Message type constants from `CommonLibSSE-NG` `MessagingInterface::kXxx`.
#[allow(non_camel_case_types, dead_code)]
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageType {
    PostLoad = 0,
    PostPostLoad = 1,
    PreLoadGame = 2,
    PostLoadGame = 3,
    SaveGame = 4,
    DeleteGame = 5,
    InputLoaded = 6,
    NewGame = 7,
    DataLoaded = 8,
}

/// `PluginVersionData` — the `SKSEPlugin_Version` data export.
///
/// Size: 0x350 bytes (verified by compile-time assert below).
#[repr(C)]
pub struct PluginVersionData {
    /// Must equal 1.
    pub data_version: u32, // 0x000
    /// Packed via `PluginVersion::pack`.
    pub plugin_version: u32, // 0x004
    /// NUL-terminated UTF-8 plugin identifier. Max 255 bytes + NUL.
    pub plugin_name: [u8; 256], // 0x008
    /// NUL-terminated UTF-8 author string.
    pub author: [u8; 256], // 0x108
    /// NUL-terminated UTF-8 support email.
    pub support_email: [u8; 252], // 0x208
    /// Flags group A — bit 0 is `no_struct_use`.
    pub flags_a: u32, // 0x304
    /// Flags group B — bit 0 = address_library, bit 1 = sig_scanning, bit 2 = structs_post629.
    pub flags_b: u32, // 0x308
    /// Zero-terminated list of packed runtime versions. `[0; 16]` signals version-agnostic.
    pub compatible_versions: [u32; 16], // 0x30C
    /// Minimum SKSE version. 0 = any.
    pub xse_minimum: u32, // 0x34C
}

const _: () = assert!(core::mem::size_of::<PluginVersionData>() == 0x350);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, data_version) == 0x000);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, plugin_version) == 0x004);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, plugin_name) == 0x008);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, author) == 0x108);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, support_email) == 0x208);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, flags_a) == 0x304);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, flags_b) == 0x308);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, compatible_versions) == 0x30C);
const _: () = assert!(core::mem::offset_of!(PluginVersionData, xse_minimum) == 0x34C);

/// Bit masks for `PluginVersionData::flags_b`.
pub mod flags_b {
    pub const ADDRESS_LIBRARY: u32 = 1 << 0;
    pub const SIG_SCANNING: u32 = 1 << 1;
    pub const STRUCTS_POST629: u32 = 1 << 2;
}

/// `PluginInfo` — out-parameter of `SKSEPlugin_Query`.
///
/// Size: 0x18 bytes (24): 4 + 4 pad + 8 (ptr) + 4 + 4 pad = 24.
#[repr(C)]
pub struct PluginInfo {
    pub info_version: u32,   // 0x00
    _pad: u32,               // 0x04 — natural padding before 8-byte pointer
    pub name: *const c_char, // 0x08
    pub version: u32,        // 0x10
    _pad2: u32,              // 0x14 — trailing padding for 8-byte struct alignment
}

const _: () = assert!(core::mem::size_of::<PluginInfo>() == 0x18);
const _: () = assert!(core::mem::offset_of!(PluginInfo, info_version) == 0x00);
const _: () = assert!(core::mem::offset_of!(PluginInfo, name) == 0x08);
const _: () = assert!(core::mem::offset_of!(PluginInfo, version) == 0x10);

impl Default for PluginInfo {
    fn default() -> Self {
        Self {
            info_version: 0,
            _pad: 0,
            name: core::ptr::null(),
            version: 0,
            _pad2: 0,
        }
    }
}

/// `SKSEInterface` — the first argument to `SKSEPlugin_Load`.
///
/// Size: 0x30 bytes.
#[repr(C)]
pub struct SKSEInterface {
    pub skse_version: u32,                                                      // 0x00
    pub runtime_version: u32,                                                   // 0x04
    pub editor_version: u32,                                                    // 0x08
    pub is_editor: u32,                                                         // 0x0C
    pub query_interface: unsafe extern "C" fn(u32) -> *mut c_void,              // 0x10
    pub get_plugin_handle: unsafe extern "C" fn() -> u32,                      // 0x18
    pub get_release_index: unsafe extern "C" fn() -> u32,                      // 0x20
    pub get_plugin_info: unsafe extern "C" fn(*const c_char) -> *const c_void, // 0x28
}

const _: () = assert!(core::mem::size_of::<SKSEInterface>() == 0x30);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, skse_version) == 0x00);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, runtime_version) == 0x04);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, editor_version) == 0x08);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, is_editor) == 0x0C);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, query_interface) == 0x10);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, get_plugin_handle) == 0x18);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, get_release_index) == 0x20);
const _: () = assert!(core::mem::offset_of!(SKSEInterface, get_plugin_info) == 0x28);

/// `SKSEMessagingInterface` — obtained by `query_interface(KMESSAGING)`.
///
/// Size: 0x20 bytes: 4 + 4 pad + 8 + 8 + 8 = 32.
#[repr(C)]
pub struct SKSEMessagingInterface {
    pub interface_version: u32, // 0x00
    _pad: u32,                  // 0x04 — natural padding before 8-byte pointer
    pub register_listener: unsafe extern "C" fn(
        handle: u32,
        sender: *const c_char,
        callback: *mut c_void,
    ) -> bool,                                                       // 0x08
    pub dispatch: unsafe extern "C" fn(
        handle: u32,
        msg_type: u32,
        data: *mut c_void,
        data_len: u32,
        receiver: *const c_char,
    ) -> bool,                                                       // 0x10
    pub get_event_dispatcher: unsafe extern "C" fn(u32) -> *mut c_void, // 0x18
}

const _: () = assert!(core::mem::size_of::<SKSEMessagingInterface>() == 0x20);
const _: () = assert!(core::mem::offset_of!(SKSEMessagingInterface, interface_version) == 0x00);
const _: () = assert!(core::mem::offset_of!(SKSEMessagingInterface, register_listener) == 0x08);
const _: () = assert!(core::mem::offset_of!(SKSEMessagingInterface, dispatch) == 0x10);
const _: () = assert!(core::mem::offset_of!(SKSEMessagingInterface, get_event_dispatcher) == 0x18);

/// Signature of an SKSE messaging callback.
///
/// The callback receives a pointer to an `SKSEMessage` and must not retain
/// the pointer past the callback's return.
pub type MessagingCallback = unsafe extern "C" fn(msg: *mut SKSEMessage);

/// `SKSEMessage` — the struct passed to a messaging-interface callback.
#[repr(C)]
pub struct SKSEMessage {
    pub sender: *const c_char,
    pub msg_type: u32,
    _pad: u32,
    pub data_len: u32,
    _pad2: u32,
    pub data: *mut c_void,
}
