#pragma once
#include "mora/emit/patch_reader.h"
#include "mora/core/string_pool.h"
#include <filesystem>

namespace mora {

struct ApplyResult {
    size_t patches_applied = 0;
    size_t patches_failed = 0;
    size_t forms_modified = 0;
    double elapsed_ms = 0;
};

class PatchApplier {
public:
    explicit PatchApplier(StringPool& pool);
    ApplyResult apply(const std::filesystem::path& patch_path);
private:
    StringPool& pool_;
};

} // namespace mora
