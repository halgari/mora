#pragma once
#include <cstdint>
#include "mora/data/form_model.h"

// Minimal runtime helpers for the harness and tests.
// Patch application is handled directly by patch_walker.cpp using
// typed CommonLibSSE-NG access — no extern "C" bridge needed.

namespace mora::rt {

// Read the form type byte from a TESForm*.
inline uint8_t get_form_type(const void* form) {
    return *reinterpret_cast<const uint8_t*>(
        reinterpret_cast<const char*>(form) + model::kFormTypeOffset);
}

// Return the offset of a scalar field within the form, or 0 if not applicable.
// Used by the harness for debugging. Not used by the patch walker.
uint64_t get_field_offset(uint8_t ft, uint16_t field_id);

// Iterate every loaded form of `form_type` (e.g. 0x2B for NPC, 0x29 for weapon)
// and invoke cb(form, ctx) for each. Uses CommonLibSSE-NG's allForms map under
// a read-lock. No-op if the game hasn't loaded yet or the DLL is built without
// CommonLib support.
void for_each_form_of_type(uint8_t form_type, void (*cb)(void* form, void* ctx), void* ctx);

// Look up a form by its FormID. Returns nullptr if not found or on non-Windows.
void* lookup_form_by_id(uint32_t formid);

} // namespace mora::rt
