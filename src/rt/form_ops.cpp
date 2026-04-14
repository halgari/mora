// Minimal runtime helpers — only get_form_type and get_field_offset remain.
// All patch application is now in patch_walker.cpp using typed CommonLibSSE-NG access.

#include "mora/rt/form_ops.h"
#include "mora/data/form_model.h"

#ifdef _WIN32
#include <RE/T/TESForm.h>
#endif

using namespace mora;

namespace mora::rt {

uint64_t get_field_offset(uint8_t ft, uint16_t field_id) {
    return model::field_offset_for(ft, static_cast<FieldId>(field_id));
}

#ifdef _WIN32
void for_each_form_of_type(uint8_t form_type, void (*cb)(void*, void*), void* ctx) {
    if (!cb) return;
    // Called from the SKSE DataLoaded hook on the main thread — forms map is
    // quiescent. Skip the BSReadLockGuard to avoid pulling CommonLib's
    // BSAtomic -> <regex>/<locale> chain that xwin's older STL can't resolve.
    auto [map, lock] = RE::TESForm::GetAllForms();
    (void)lock;
    if (!map) return;
    for (auto& kv : *map) {
        RE::TESForm* form = kv.second;
        if (!form) continue;
        if (get_form_type(form) != form_type) continue;
        cb(form, ctx);
    }
}
void* lookup_form_by_id(uint32_t formid) {
    return RE::TESForm::LookupByID(formid);
}
#else
void for_each_form_of_type(uint8_t, void (*)(void*, void*), void*) {}
void* lookup_form_by_id(uint32_t) { return nullptr; }
#endif

} // namespace mora::rt
