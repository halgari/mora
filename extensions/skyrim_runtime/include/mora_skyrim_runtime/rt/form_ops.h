#pragma once

#include "mora/data/form_model.h"

#include <cstdint>

namespace mora_skyrim_runtime::rt {

// Read the form type byte from a TESForm*.
inline uint8_t get_form_type(const void* form) {
    return *reinterpret_cast<const uint8_t*>(
        reinterpret_cast<const char*>(form) + mora::model::kFormTypeOffset);
}

// Iterate every loaded form of `form_type` (e.g. 0x2B for NPC, 0x29 for
// weapon) and invoke cb(form, ctx) for each. Uses CommonLibSSE-NG's
// allForms map. No-op when built without CommonLib.
void for_each_form_of_type(uint8_t form_type,
                            void (*cb)(void* form, void* ctx),
                            void* ctx);

// Look up a form by its FormID. Returns nullptr if not found or on
// non-Windows builds.
void* lookup_form_by_id(uint32_t formid);

} // namespace mora_skyrim_runtime::rt
