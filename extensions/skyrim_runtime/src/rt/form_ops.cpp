// Windows-only (MORA_WITH_COMMONLIB): form iteration helpers that
// reach into CommonLibSSE-NG's TESForm machinery.

#include "mora_skyrim_runtime/rt/form_ops.h"

#ifdef MORA_WITH_COMMONLIB
#include <RE/T/TESForm.h>
#endif

namespace mora_skyrim_runtime::rt {

#ifdef MORA_WITH_COMMONLIB
void for_each_form_of_type(uint8_t form_type,
                            void (*cb)(void* form, void* ctx),
                            void* ctx)
{
    if (!cb) return;
    // Called from the SKSE DataLoaded hook on the main thread — forms
    // map is quiescent. Skip BSReadLockGuard to avoid pulling CommonLib's
    // BSAtomic → <regex>/<locale> chain that xwin's older STL can't resolve.
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

} // namespace mora_skyrim_runtime::rt
