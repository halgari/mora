//! `BGSKeywordForm` partial layout + `add_keyword` re-implementation.
//!
//! Ported from `CommonLibSSE-NG/src/RE/B/BGSKeywordForm.cpp`. Mora
//! does NOT call into game code for `AddKeyword` — that method is
//! inlined in CommonLibSSE-NG and has no Address Library ID. We
//! replicate the algorithm in Rust using Skyrim's `MemoryManager`
//! allocator (the same allocator the game uses for the keywords
//! array, so `free` semantics remain compatible).
//!
//! M1-minimal: only `add_keyword` is implemented. `has_keyword`,
//! `remove_keyword`, `add_keywords` batch variant — all deferred.

use crate::game::keyword::BGSKeyword;
use crate::game::memory::{allocate, deallocate};
use crate::relocation::RelocationError;

/// `BGSKeywordForm` is a mixin component — not a standalone form.
/// Forms that support keywords (Weapon, Armor, NPC, …) multiply-inherit
/// from both `TESForm` and `BGSKeywordForm`. The sub-object offset
/// within the owning form is type-specific.
///
/// Plan 3 binds `BGSKeywordForm` in isolation. The smoke test obtains
/// a `*mut BGSKeywordForm` by casting a `*mut TESForm` + a known
/// offset; see `skse-rs-smoke` Task 14 for the verified offset.
///
/// Size 0x18 per CommonLibSSE-NG.
#[repr(C)]
pub struct BGSKeywordForm {
    pub vtable: *const (),
    pub keywords: *mut *mut BGSKeyword,
    pub num_keywords: u32,
    pub _pad_14: u32,
}

const _: () = assert!(core::mem::size_of::<BGSKeywordForm>() == 0x18);

/// Errors from [`add_keyword`].
#[derive(Debug, thiserror::Error)]
pub enum AddKeywordError {
    #[error("relocation: {0}")]
    Relocation(#[from] RelocationError),
    #[error("keyword allocator returned null for size {0}")]
    AllocatorFailed(usize),
}

/// Add `keyword` to `form`, returning `true` on insertion, `false` if
/// the keyword was already present (no mutation).
///
/// Algorithm (ported verbatim from CommonLibSSE-NG):
/// 1. Linear-scan existing keywords; if present, return `Ok(false)`.
/// 2. Allocate a new `BGSKeyword**` array of `num_keywords + 1` via
///    `MemoryManager::Allocate`.
/// 3. Copy old pointers + append the new one.
/// 4. Atomically swap the pointer and length (ordered).
/// 5. Free the old array (if it was non-null) via `MemoryManager::Deallocate`.
///
/// # Safety
/// - `form` must point to a valid `BGSKeywordForm` sub-object.
/// - `keyword` must point to a valid `BGSKeyword`.
/// - Must be called on the main thread during `kDataLoaded` or later,
///   when no other thread concurrently mutates this form's keywords.
pub unsafe fn add_keyword(
    form: *mut BGSKeywordForm,
    keyword: *mut BGSKeyword,
) -> Result<bool, AddKeywordError> {
    // Step 1: dedup.
    {
        let form_ref = unsafe { &*form };
        let n = form_ref.num_keywords as usize;
        let arr = form_ref.keywords;
        for i in 0..n {
            let existing = unsafe { *arr.add(i) };
            if existing == keyword {
                return Ok(false);
            }
        }
    }

    // Step 2: allocate new array of (n+1) pointers.
    let n = unsafe { (*form).num_keywords as usize };
    let new_count = n + 1;
    let size_bytes = new_count
        .checked_mul(core::mem::size_of::<*mut BGSKeyword>())
        .ok_or(AddKeywordError::AllocatorFailed(usize::MAX))?;
    let new_arr_raw = unsafe { allocate(size_bytes)? };
    if new_arr_raw.is_null() {
        return Err(AddKeywordError::AllocatorFailed(size_bytes));
    }
    let new_arr: *mut *mut BGSKeyword = new_arr_raw as *mut *mut BGSKeyword;

    // Step 3: copy + append.
    let old_arr = unsafe { (*form).keywords };
    for i in 0..n {
        unsafe {
            let p = *old_arr.add(i);
            *new_arr.add(i) = p;
        }
    }
    unsafe { *new_arr.add(n) = keyword };

    // Step 4: swap. Order matters: write `keywords` before `num_keywords`
    // so any concurrent reader never sees num_keywords past the end of a
    // shorter array. Not that we expect concurrent readers at kDataLoaded,
    // but defensive is cheap.
    let old_ptr = old_arr;
    unsafe {
        (*form).keywords = new_arr;
        (*form).num_keywords = new_count as u32;
    }

    // Step 5: free old.
    if !old_ptr.is_null() {
        unsafe { deallocate(old_ptr as *mut u8)? };
    }
    Ok(true)
}
