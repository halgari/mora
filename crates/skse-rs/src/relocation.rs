//! Address-library-backed relocation resolver.
//!
//! Skyrim's image base is obtained from `GetModuleHandleW(NULL)` — the
//! currently loaded executable. Plan 3 keeps this Windows-only; a
//! future plan may abstract it if we need cross-platform tests.
//!
//! Plan 3 loads the Address Library bin lazily on first `Relocation::id`
//! call. The bin path is configurable via [`Relocation::set_library_path`];
//! default is the standard SKSE Plugins location.

use crate::address_library::{AddressLibrary, AddressLibraryError};
use std::path::PathBuf;
use std::sync::OnceLock;

/// Errors from resolving a relocation.
#[derive(Debug, thiserror::Error)]
pub enum RelocationError {
    #[error("image base could not be resolved")]
    ImageBase,
    #[error("address library: {0}")]
    Library(#[from] AddressLibraryError),
    #[error("address library not initialized (call Relocation::set_library first)")]
    NotInitialized,
}

static LIBRARY: OnceLock<AddressLibrary> = OnceLock::new();

/// Provide the loaded Address Library. Call once during plugin init
/// (typically inside `on_load`). Subsequent calls are ignored.
pub fn set_library(library: AddressLibrary) {
    let _ = LIBRARY.set(library);
}

/// Initialize the library from a file path. Convenience over
/// [`set_library`] for the common case.
pub fn load_library_from_path(path: &std::path::Path) -> Result<(), RelocationError> {
    let lib = AddressLibrary::load(path)?;
    set_library(lib);
    Ok(())
}

/// Resolve the well-known SKSE Plugins path, using `%MORA_SKYRIM_DATA%`
/// if set, else the first candidate that exists.
pub fn resolve_default_library_path() -> Option<PathBuf> {
    if let Ok(data) = std::env::var("MORA_SKYRIM_DATA") {
        let p = PathBuf::from(data).join("SKSE/Plugins/versionlib-1-6-1170-0.bin");
        if p.exists() {
            return Some(p);
        }
    }
    // On Windows, the plugin's working directory is the game's Data dir.
    let p = PathBuf::from("SKSE/Plugins/versionlib-1-6-1170-0.bin");
    if p.exists() {
        return Some(p);
    }
    None
}

/// A resolved address — in bytes from the image base.
///
/// Construct via [`Relocation::id`]. Use [`Relocation::ptr`] to get a
/// raw pointer, or the typed helpers to cast to a function pointer
/// with a known signature.
#[derive(Debug, Clone, Copy)]
pub struct Relocation {
    /// Absolute address in the current process.
    addr: usize,
}

impl Relocation {
    /// Resolve an Address Library id to a process-absolute address.
    ///
    /// # Safety
    /// The returned address must be used consistently with the
    /// function/variable layout documented for the id.
    pub fn id(id: u64) -> Result<Relocation, RelocationError> {
        let lib = LIBRARY.get().ok_or(RelocationError::NotInitialized)?;
        let offset = lib.resolve(id)?;
        let base = image_base_address().ok_or(RelocationError::ImageBase)?;
        Ok(Relocation { addr: base + offset as usize })
    }

    /// Raw address as `usize`.
    pub fn addr(self) -> usize {
        self.addr
    }

    /// Cast the address to `*const T`.
    ///
    /// # Safety
    /// T must match the in-memory layout of whatever the id resolves to.
    pub unsafe fn as_ptr<T>(self) -> *const T {
        self.addr as *const T
    }

    /// Cast the address to `*mut T`.
    ///
    /// # Safety
    /// T must match the in-memory layout of whatever the id resolves to.
    pub unsafe fn as_mut_ptr<T>(self) -> *mut T {
        self.addr as *mut T
    }

    /// Cast to a function pointer. The caller must know the full
    /// signature; `FnPtr` is typically `unsafe extern "C" fn(...) -> ...`.
    ///
    /// # Safety
    /// Caller certifies that the target address is a function with the
    /// claimed signature.
    pub unsafe fn as_fn<FnPtr: Copy>(self) -> FnPtr {
        debug_assert_eq!(
            core::mem::size_of::<FnPtr>(),
            core::mem::size_of::<usize>(),
            "FnPtr must be pointer-sized"
        );
        unsafe { core::mem::transmute_copy(&self.addr) }
    }
}

#[cfg(windows)]
fn image_base_address() -> Option<usize> {
    use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
    unsafe {
        let h = GetModuleHandleW(core::ptr::null());
        if h.is_null() {
            None
        } else {
            Some(h as usize)
        }
    }
}

#[cfg(not(windows))]
fn image_base_address() -> Option<usize> {
    // Non-Windows (dev-box unit tests): pretend the image base is 0 so
    // `Relocation::id` returns the raw rva. Useful for testing the
    // resolver plumbing without a real game binary.
    Some(0)
}
