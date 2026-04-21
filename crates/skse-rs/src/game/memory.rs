//! `MemoryManager` bindings for Skyrim's internal allocator.
//!
//! M1-minimal: only `GetSingleton`, `Allocate`, `Deallocate` are
//! bound — just enough for `BGSKeywordForm::add_keyword`.

use crate::relocation::{Relocation, RelocationError};

/// Opaque MemoryManager. We never construct or access fields — only
/// pass a pointer to `Allocate` / `Deallocate`.
#[repr(C)]
pub struct MemoryManager {
    _private: [u8; 0],
}

pub mod ae_ids {
    pub const GET_SINGLETON: u64 = 11141;
    pub const ALLOCATE: u64 = 68115;
    pub const DEALLOCATE: u64 = 68117;
}

type GetSingletonFn = unsafe extern "C" fn() -> *mut MemoryManager;
type AllocateFn = unsafe extern "C" fn(
    mm: *mut MemoryManager,
    size: usize,
    alignment: u32,
    aligned: bool,
) -> *mut u8;
type DeallocateFn = unsafe extern "C" fn(
    mm: *mut MemoryManager,
    ptr: *mut u8,
    aligned: bool,
);

/// Resolve the `MemoryManager` singleton.
///
/// # Safety
/// Must be called after `Relocation::set_library` has been invoked.
pub unsafe fn get_singleton() -> Result<*mut MemoryManager, RelocationError> {
    let r = Relocation::id(ae_ids::GET_SINGLETON)?;
    let f: GetSingletonFn = unsafe { r.as_fn() };
    Ok(unsafe { f() })
}

/// Allocate `size` bytes from Skyrim's internal heap. Matches
/// `RE::malloc(size)`: alignment=0, aligned=false.
///
/// # Safety
/// Caller must `deallocate` the returned pointer via [`deallocate`];
/// don't pair with `HeapFree` or Rust's `alloc::dealloc`.
pub unsafe fn allocate(size: usize) -> Result<*mut u8, RelocationError> {
    let mm = unsafe { get_singleton() }?;
    let r = Relocation::id(ae_ids::ALLOCATE)?;
    let f: AllocateFn = unsafe { r.as_fn() };
    Ok(unsafe { f(mm, size, 0, false) })
}

/// Free memory previously allocated via [`allocate`].
///
/// # Safety
/// `ptr` must have come from [`allocate`]; double-free is UB.
pub unsafe fn deallocate(ptr: *mut u8) -> Result<(), RelocationError> {
    if ptr.is_null() {
        return Ok(());
    }
    let mm = unsafe { get_singleton() }?;
    let r = Relocation::id(ae_ids::DEALLOCATE)?;
    let f: DeallocateFn = unsafe { r.as_fn() };
    unsafe { f(mm, ptr, false) };
    Ok(())
}
