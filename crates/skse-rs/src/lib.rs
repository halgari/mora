//! `skse-rs` — Rust-native SKSE plugin framework.
//!
//! Clean-room Rust port of the SKSE plugin infrastructure Mora needs.
//! Not a binding to the C++ CommonLibSSE-NG library; every ABI type
//! is defined directly in Rust with layout validated by compile-time
//! size asserts.
//!
//! See `docs/src/skse-rs-ffi-reference.md` for the source-of-truth
//! layout reference.
//!
//! This crate is no-std-friendly in principle but currently relies on
//! `std` for logging file I/O.
//!
//! # M1 Foundation scope
//!
//! Plan 2 (this milestone's first half) implements:
//! - ABI types (`PluginVersionData`, `PluginInfo`, `SKSEInterface`,
//!   `SKSEMessagingInterface`, messaging IDs).
//! - The three plugin exports (`SKSEPlugin_Version`,
//!   `SKSEPlugin_Query`, `SKSEPlugin_Load`).
//! - File-based logger writing to the Skyrim log directory.
//! - `SksePlugin` trait + `declare_plugin!` macro for downstream
//!   crates to opt in cleanly.
//!
//! Plan 3 adds: Address Library parser, relocation layer, game type
//! layouts, `TESDataHandler` form lookup, `AddKeyword` re-implementation.

pub mod address_library;
pub mod ffi;
pub mod game;
pub mod log;
pub mod messaging;
pub mod plugin;
pub mod relocation;
pub mod version;

pub use log::{LogInitError, Logger};
pub use plugin::{LoadError, LoadOutcome, SksePlugin};
pub use version::{PluginVersion, RuntimeVersion};

/// Emit the three C-ABI exports (`SKSEPlugin_Version`,
/// `SKSEPlugin_Query`, `SKSEPlugin_Load`) for a type implementing
/// [`SksePlugin`].
///
/// ```ignore
/// use skse_rs::{declare_plugin, PluginVersion, SksePlugin, LoadOutcome};
/// use skse_rs::ffi::SKSEInterface;
///
/// struct MyPlugin;
/// impl SksePlugin for MyPlugin {
///     const NAME: &'static str = "MyPlugin";
///     const VERSION: PluginVersion = PluginVersion {
///         major: 0, minor: 1, patch: 0, build: 0,
///     };
///     unsafe fn on_load(_skse: &'static SKSEInterface) -> LoadOutcome {
///         Ok(())
///     }
/// }
///
/// declare_plugin!(MyPlugin);
/// ```
#[macro_export]
macro_rules! declare_plugin {
    ($plugin_ty:ty) => {
        /// Helper: copy `src` (a `&str`) into a zero-initialized `[u8; N]`
        /// byte buffer, NUL-terminated. Truncates with NUL fill if too long.
        const fn __skse_rs_copy_nul<const N: usize>(src: &str) -> [u8; N] {
            let mut out = [0u8; N];
            let bytes = src.as_bytes();
            let copy_len = if bytes.len() >= N { N - 1 } else { bytes.len() };
            let mut i = 0;
            while i < copy_len {
                out[i] = bytes[i];
                i += 1;
            }
            out
        }

        /// `SKSEPlugin_Version` — the plugin-info data export.
        #[allow(non_upper_case_globals)]
        #[unsafe(no_mangle)]
        pub static SKSEPlugin_Version: $crate::ffi::PluginVersionData =
            $crate::ffi::PluginVersionData {
                data_version: 1,
                plugin_version: <$plugin_ty as $crate::SksePlugin>::VERSION.pack(),
                plugin_name: __skse_rs_copy_nul::<256>(<$plugin_ty as $crate::SksePlugin>::NAME),
                author: __skse_rs_copy_nul::<256>(<$plugin_ty as $crate::SksePlugin>::AUTHOR),
                support_email: __skse_rs_copy_nul::<252>(
                    <$plugin_ty as $crate::SksePlugin>::SUPPORT_EMAIL,
                ),
                flags_a: 0,
                // ADDRESS_LIBRARY: relocations go through Address Library.
                // STRUCTS_POST629: declares compat with Skyrim 1.6.629+ (AE
                // runtime layout). Required by SKSE to load the DLL on the
                // AE runtime when `compatible_versions` is otherwise empty.
                flags_b: $crate::ffi::flags_b::ADDRESS_LIBRARY
                    | $crate::ffi::flags_b::STRUCTS_POST629,
                compatible_versions: [0; 16],
                xse_minimum: 0,
            };

        /// The canonical plugin name, as a static NUL-terminated C string.
        #[allow(non_upper_case_globals)]
        static __SKSE_RS_PLUGIN_NAME_C: [u8; 256] =
            __skse_rs_copy_nul::<256>(<$plugin_ty as $crate::SksePlugin>::NAME);

        /// Legacy entry point — still required by SKSE.
        ///
        /// # Safety
        /// Called by SKSE's plugin loader with valid non-null pointers.
        #[allow(non_snake_case)]
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn SKSEPlugin_Query(
            _skse: *const $crate::ffi::SKSEInterface,
            info: *mut $crate::ffi::PluginInfo,
        ) -> bool {
            if info.is_null() {
                return false;
            }
            unsafe {
                *info = $crate::ffi::PluginInfo {
                    info_version: 1,
                    _pad: 0,
                    name: __SKSE_RS_PLUGIN_NAME_C.as_ptr() as *const core::ffi::c_char,
                    version: <$plugin_ty as $crate::SksePlugin>::VERSION.pack(),
                    _pad2: 0,
                };
            }
            true
        }

        /// Generated SKSE messaging callback. Dispatches `kDataLoaded`
        /// to the plugin's `on_data_loaded` method; ignores all other
        /// SKSE messages.
        ///
        /// # Safety
        /// Called by SKSE on the main thread.
        #[allow(non_snake_case)]
        unsafe extern "C" fn __skse_rs_messaging_callback(msg: *mut $crate::ffi::SKSEMessage) {
            if unsafe { $crate::messaging::is_data_loaded(msg) } {
                unsafe { <$plugin_ty as $crate::SksePlugin>::on_data_loaded() };
            }
        }

        /// Real entry point — SKSE calls this with a valid interface.
        ///
        /// # Safety
        /// Called by SKSE's plugin loader; `skse` is valid for the lifetime
        /// of the DLL.
        #[allow(non_snake_case)]
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn SKSEPlugin_Load(skse: *const $crate::ffi::SKSEInterface) -> bool {
            if skse.is_null() {
                return false;
            }
            // SAFETY: SKSE guarantees this pointer is valid for the DLL lifetime.
            let skse_ref: &'static $crate::ffi::SKSEInterface = unsafe { &*skse };
            // Run user on_load first so it can set up logging / load
            // address libraries before we register the messaging listener.
            match unsafe { <$plugin_ty as $crate::SksePlugin>::on_load(skse_ref) } {
                Ok(()) => {}
                Err(_) => return false,
            }
            // Register kDataLoaded listener.
            let messaging = match unsafe { $crate::messaging::get_messaging(skse_ref) } {
                Ok(m) => m,
                Err(_) => return false,
            };
            match unsafe {
                $crate::messaging::register_listener(
                    skse_ref,
                    messaging,
                    __skse_rs_messaging_callback,
                )
            } {
                Ok(()) => true,
                Err(_) => false,
            }
        }
    };
}
