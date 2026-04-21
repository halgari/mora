//! `TESForm` partial layout + `lookup_by_id`.
//!
//! M1-minimal: only `form_id` is exposed. Vtable + sourceFiles +
//! flags are opaque. Future plans add accessors as needed.

use crate::game::hash_map::FormHashMap;
use crate::game::lock::{BSReadWriteLock, ReadGuard};
use crate::relocation::{Relocation, RelocationError};

/// Address Library IDs for the global form map + its lock.
pub mod ae_ids {
    /// Global slot holding a `BSTHashMap<FormID, *mut TESForm>*`.
    /// Note: this is a *pointer to a pointer* — dereference once to
    /// get the map pointer.
    pub const ALL_FORMS: u64 = 400507;
    /// Read-write lock guarding `ALL_FORMS`.
    pub const ALL_FORMS_LOCK: u64 = 400517;
}

/// Alias: a FormID is a 32-bit Bethesda global-unique ID.
pub type FormID = u32;

/// `TESForm` partial layout. Size 0x20 (asserted).
///
/// M1-minimal: only `form_id` is used; the rest is opaque. Vtable
/// dispatch, flags, and source-file accessors are deferred until a
/// real consumer needs them.
#[repr(C)]
pub struct TESForm {
    pub vtable: *const (),
    pub source_files: *mut (),
    pub form_flags: u32,
    pub form_id: FormID,
    pub in_game_flags: u16,
    pub form_type: u8,
    pub _pad_1b: u8,
    pub _pad_1c: u32,
}

const _: () = assert!(core::mem::size_of::<TESForm>() == 0x20);
const _: () = assert!(core::mem::offset_of!(TESForm, form_id) == 0x14);

/// Look up a form by its FormID. Returns `Ok(None)` if the form is
/// not in the global map, `Err` on infrastructure failure (address
/// library not loaded, required ids missing, image base unavailable).
///
/// # Safety
/// Must be called on the main thread during a time when the form map
/// exists (e.g., after `kDataLoaded` fires). The returned pointer,
/// if any, is valid for the lifetime of the process (forms don't move
/// once loaded).
pub unsafe fn lookup_by_id(form_id: FormID) -> Result<Option<*mut TESForm>, RelocationError> {
    // Resolve the all-forms pointer-to-pointer and the lock.
    let all_forms_pp_reloc = Relocation::id(ae_ids::ALL_FORMS)?;
    let lock_reloc = Relocation::id(ae_ids::ALL_FORMS_LOCK)?;

    let all_forms_pp: *mut *mut FormHashMap = unsafe { all_forms_pp_reloc.as_mut_ptr() };
    let lock: *mut BSReadWriteLock = unsafe { lock_reloc.as_mut_ptr() };

    // Guard with read lock (released on Drop).
    let _guard = unsafe { ReadGuard::new(lock)? };

    let map_ptr: *mut FormHashMap = unsafe { *all_forms_pp };
    if map_ptr.is_null() {
        return Ok(None);
    }

    let map: &FormHashMap = unsafe { &*map_ptr };
    let result = unsafe { map.lookup(form_id) };
    if result.is_null() {
        Ok(None)
    } else {
        Ok(Some(result))
    }
}
