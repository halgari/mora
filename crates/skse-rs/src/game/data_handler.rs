//! `TESDataHandler` singleton access.
//!
//! M1-minimal: only `get_singleton` is exposed. Inner fields are
//! opaque — the smoke test doesn't read them (it uses TESForm
//! lookup_by_id, which goes through the global allForms map, not
//! through TESDataHandler).

use crate::relocation::{Relocation, RelocationError};

/// Opaque TESDataHandler. No inner fields are exposed at M1.
#[repr(C)]
pub struct TESDataHandler {
    _private: [u8; 0],
}

pub mod ae_ids {
    /// Static variable holding a `TESDataHandler*`. Deref once.
    pub const SINGLETON: u64 = 400269;
}

/// Resolve the singleton pointer.
///
/// # Safety
/// Must be called after `Relocation::set_library` has been invoked.
/// The returned pointer, if non-null, lives for the process lifetime.
pub unsafe fn get_singleton() -> Result<*mut TESDataHandler, RelocationError> {
    let r = Relocation::id(ae_ids::SINGLETON)?;
    // The id resolves to a static variable slot: address-of-the-pointer.
    let pp: *mut *mut TESDataHandler = unsafe { r.as_mut_ptr() };
    if pp.is_null() {
        return Ok(core::ptr::null_mut());
    }
    Ok(unsafe { *pp })
}
