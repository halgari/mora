//! `BGSKeyword` partial layout.
//!
//! M1-minimal: keywords are identified by their `TESForm` base (FormID).
//! `form_editor_id: BSFixedString` is not bound — the smoke test
//! doesn't need it.

use crate::game::form::TESForm;

/// `BGSKeyword` partial layout. Size 0x28 per CommonLibSSE-NG.
#[repr(C)]
pub struct BGSKeyword {
    pub base: TESForm,         // 0x00 — inline TESForm
    pub form_editor_id: usize, // 0x20 — BSFixedString is a single pointer
}

const _: () = assert!(core::mem::size_of::<BGSKeyword>() == 0x28);
const _: () = assert!(core::mem::offset_of!(BGSKeyword, base) == 0x00);
