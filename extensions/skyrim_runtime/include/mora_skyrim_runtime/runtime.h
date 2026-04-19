#pragma once

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <filesystem>

namespace mora_skyrim_runtime {

class GameAPI;

// Read the four skyrim/{set,add,remove,multiply}.parquet files under
// `parquet_dir` and dispatch each row through `api`. `pool` is used
// internally for interning strings encountered in the input (the API
// passes StringId-less Values so the pool is only needed for the
// internal parquet-reader bookkeeping).
//
// Missing per-op parquet files are non-fatal — emits a warning
// diagnostic and skips. Malformed files (schema mismatch, bad column
// types) produce an error diagnostic and skip that relation.
//
// Returns the total number of effect facts successfully dispatched.
size_t runtime_apply(const std::filesystem::path& parquet_dir,
                      GameAPI&                     api,
                      mora::StringPool&            pool,
                      mora::DiagBag&               diags);

} // namespace mora_skyrim_runtime
