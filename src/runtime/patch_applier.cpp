#include "mora/runtime/patch_applier.h"
#include "mora/runtime/form_bridge.h"
#include <chrono>
#include <fstream>

namespace mora {

PatchApplier::PatchApplier(StringPool& pool)
    : pool_(pool) {}

ApplyResult PatchApplier::apply(const std::filesystem::path& patch_path) {
    ApplyResult result;
    auto start = std::chrono::steady_clock::now();

    std::ifstream file(patch_path, std::ios::binary);
    if (!file.is_open()) {
        return result;
    }

    PatchReader reader(pool_);
    auto patch_file = reader.read(file);
    if (!patch_file) {
        return result;
    }

    for (const auto& resolved : patch_file->patches) {
        int applied = FormBridge::apply_patches(resolved.target_formid, resolved.fields);
        result.patches_applied += static_cast<size_t>(applied);
        result.patches_failed += resolved.fields.size() - static_cast<size_t>(applied);
        if (applied > 0) {
            ++result.forms_modified;
        }
    }

    auto end = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

} // namespace mora
