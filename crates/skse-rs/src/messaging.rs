//! SKSE messaging-interface wrapper.
//!
//! Exposes `register_data_loaded_listener` — registers a Rust
//! function to be invoked when SKSE broadcasts `kDataLoaded` (msg
//! type 8), which fires after all plugins and forms have loaded.
//!
//! Plan 3 only wires `kDataLoaded`. Future plans may add other event
//! types (kPreLoadGame, kPostLoadGame, kSaveGame, etc.).

use crate::ffi::{
    KMESSAGING, MessageType, MessagingCallback, SKSEInterface, SKSEMessage, SKSEMessagingInterface,
};
use crate::plugin::LoadError;

/// Obtain the SKSE messaging interface from the passed SKSEInterface.
///
/// # Safety
/// `skse` must be valid.
pub unsafe fn get_messaging(
    skse: &SKSEInterface,
) -> Result<&'static SKSEMessagingInterface, LoadError> {
    let ptr = unsafe { (skse.query_interface)(KMESSAGING) } as *const SKSEMessagingInterface;
    if ptr.is_null() {
        return Err(LoadError::MessagingUnavailable);
    }
    Ok(unsafe { &*ptr })
}

/// Register a listener for SKSE *system* messages (kPostLoad,
/// kPostLoadGame, kDataLoaded, kNewGame, kSaveGame, kPreLoadGame,
/// kDeleteGame). `callback` receives every SKSE broadcast message — it
/// must filter on `msg.msg_type`.
///
/// Subscribes to sender `"SKSE"` (the canonical SKSE system-message
/// sender). Passing a null sender registers for messages from *no
/// specific plugin*, which silently drops system broadcasts. Every
/// sample SKSE plugin registers for `"SKSE"`.
///
/// # Safety
/// The callback must not store the message pointer past its own
/// return (SKSE owns the message memory).
pub unsafe fn register_listener(
    skse: &SKSEInterface,
    messaging: &SKSEMessagingInterface,
    callback: MessagingCallback,
) -> Result<(), LoadError> {
    let handle = unsafe { (skse.get_plugin_handle)() };
    let callback_void = callback as *mut core::ffi::c_void;
    // "SKSE\0" as the sender so we receive kDataLoaded et al.
    const SKSE_SENDER: &[u8] = b"SKSE\0";
    let ok = unsafe {
        (messaging.register_listener)(
            handle,
            SKSE_SENDER.as_ptr() as *const core::ffi::c_char,
            callback_void,
        )
    };
    if ok {
        Ok(())
    } else {
        Err(LoadError::ListenerRegistrationFailed)
    }
}

/// Check whether an `SKSEMessage` is the `kDataLoaded` broadcast.
///
/// # Safety
/// `msg` must be a valid pointer to an `SKSEMessage` (from SKSE).
pub unsafe fn is_data_loaded(msg: *mut SKSEMessage) -> bool {
    if msg.is_null() {
        return false;
    }
    unsafe { (*msg).msg_type == MessageType::DataLoaded as u32 }
}
