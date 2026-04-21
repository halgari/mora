//! Stub. Populated in Task 9 of Plan 3.
//!
//! Task 8 forward-declares `TESForm` as an opaque type so
//! `game::hash_map` can reference it via pointer.

/// Opaque forward-declaration. Full layout lands in Task 9.
#[repr(C)]
pub struct TESForm {
    _private: [u8; 0],
}
