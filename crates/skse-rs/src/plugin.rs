//! The idiomatic Rust entry point that plugin crates implement.
//!
//! Downstream crates implement [`SksePlugin`] and register via the
//! [`declare_plugin!`] macro, which generates the three required
//! C-ABI exports (`SKSEPlugin_Version`, `SKSEPlugin_Query`,
//! `SKSEPlugin_Load`) targeting an impl of this trait.

use crate::ffi::SKSEInterface;
use crate::version::PluginVersion;

/// Errors that can occur during plugin load. Returning `Err` from
/// [`SksePlugin::on_load`] causes the Rust-side macro to return
/// `false` from `SKSEPlugin_Load`, which SKSE interprets as a failed
/// plugin init (the DLL is unloaded).
#[derive(Debug, thiserror::Error)]
pub enum LoadError {
    #[error("log init failed: {0}")]
    LogInit(#[from] crate::log::LogInitError),
    #[error("SKSE messaging interface unavailable")]
    MessagingUnavailable,
    #[error("listener registration failed")]
    ListenerRegistrationFailed,
    #[error("plugin-specific: {0}")]
    Other(String),
}

/// Result alias used by [`SksePlugin::on_load`].
pub type LoadOutcome = Result<(), LoadError>;

/// Implemented by downstream SKSE plugins to expose their metadata and
/// init logic to the `skse-rs` ABI layer.
pub trait SksePlugin {
    /// Plugin identifier. Must be <= 255 bytes (NUL-terminated).
    const NAME: &'static str;
    /// Plugin author. <= 255 bytes (NUL-terminated).
    const AUTHOR: &'static str = "";
    /// Support email. <= 251 bytes (NUL-terminated).
    const SUPPORT_EMAIL: &'static str = "";
    /// Plugin version.
    const VERSION: PluginVersion;

    /// Called from `SKSEPlugin_Load`. The passed interface pointer is
    /// valid until the DLL unloads and is safe to stash in a static
    /// cell.
    ///
    /// # Safety
    /// `skse` must point to a valid `SKSEInterface` provided by SKSE.
    unsafe fn on_load(skse: &'static SKSEInterface) -> LoadOutcome;
}
