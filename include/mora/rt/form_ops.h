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
uint64_t get_field_offset(uint8_t form_type, uint16_t field_id);

} // namespace mora::rt
