//! `BSReadWriteLock` binding.
//!
//! M1-minimal: only `LockForRead` / `UnlockForRead` are bound.
//! Write-lock IDs listed for future use.

use crate::relocation::{Relocation, RelocationError};

/// AE Address Library IDs for the four lock methods.
pub mod ae_ids {
    pub const LOCK_FOR_READ: u64 = 68233;
    pub const UNLOCK_FOR_READ: u64 = 68239;
    pub const LOCK_FOR_WRITE: u64 = 68234;
    pub const UNLOCK_FOR_WRITE: u64 = 68240;
}

/// Layout of `RE::BSReadWriteLock`. Size 0x08.
#[repr(C)]
pub struct BSReadWriteLock {
    pub writer_thread: u32,
    pub lock: u32,
}

const _: () = assert!(core::mem::size_of::<BSReadWriteLock>() == 0x08);

type LockForReadFn = unsafe extern "C" fn(lock: *mut BSReadWriteLock);
type UnlockForReadFn = unsafe extern "C" fn(lock: *mut BSReadWriteLock);

/// Acquire the read lock.
///
/// # Safety
/// `lock` must point to a valid `BSReadWriteLock` instance owned by
/// Skyrim. Must be balanced with [`unlock_for_read`].
pub unsafe fn lock_for_read(lock: *mut BSReadWriteLock) -> Result<(), RelocationError> {
    let r = Relocation::id(ae_ids::LOCK_FOR_READ)?;
    let f: LockForReadFn = unsafe { r.as_fn() };
    unsafe { f(lock) };
    Ok(())
}

/// Release the read lock.
///
/// # Safety
/// `lock` must be a previously acquired read lock via [`lock_for_read`].
pub unsafe fn unlock_for_read(lock: *mut BSReadWriteLock) -> Result<(), RelocationError> {
    let r = Relocation::id(ae_ids::UNLOCK_FOR_READ)?;
    let f: UnlockForReadFn = unsafe { r.as_fn() };
    unsafe { f(lock) };
    Ok(())
}

/// RAII read-lock guard. Drops call `UnlockForRead`.
pub struct ReadGuard {
    lock: *mut BSReadWriteLock,
}

impl ReadGuard {
    /// # Safety
    /// `lock` must be a valid, live `BSReadWriteLock`.
    pub unsafe fn new(lock: *mut BSReadWriteLock) -> Result<Self, RelocationError> {
        unsafe { lock_for_read(lock)? };
        Ok(ReadGuard { lock })
    }
}

impl Drop for ReadGuard {
    fn drop(&mut self) {
        unsafe {
            // Best-effort: ignore error in Drop. If relocation isn't
            // available during Drop, something has gone badly wrong
            // already; falling through doesn't make it worse.
            let _ = unlock_for_read(self.lock);
        }
    }
}
