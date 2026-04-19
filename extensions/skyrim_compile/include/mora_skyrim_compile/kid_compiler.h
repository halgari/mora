#pragma once

#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace mora_skyrim_compile {

// Inputs to KID rule synthesis.
//
// `data_dir` and `kid_dir` follow the LoadCtx semantics: `kid_dir`, if
// non-empty, overrides `data_dir` for *_KID.ini discovery.
//
// `editor_ids` is the EditorID -> FormID map populated by
// SkyrimEspDataSource (compile_kid_modules reads it for resolution).
//
// `plugin_runtime_index` (optional) maps lowercase plugin filename to
// the packed runtime descriptor for FormID-ref resolution. When null,
// `0xFFF~Mod.esp` lines drop with `kid-formid-unsupported`.
struct KidCompileInputs {
    std::filesystem::path                                   data_dir;
    std::filesystem::path                                   kid_dir;          // empty = use data_dir
    const std::unordered_map<std::string, uint32_t>*        editor_ids = nullptr;
    const std::unordered_map<std::string, uint32_t>*        plugin_runtime_index = nullptr;
};

// Output of KID rule synthesis.
struct KidCompileResult {
    // Synthesized module containing one rule per KID line per OR-group.
    // Empty (no rules, no namespace) when KID is disabled, no
    // *_KID.ini files exist, or all lines failed to resolve.
    mora::Module module;

    // Synthetic EditorID names (`__kid_formid_<8hex>`) the caller must
    // register in the evaluator's symbol table so EditorIdExpr nodes in
    // `module.rules` resolve to the correct runtime FormIDs. Real
    // EditorIDs already resolve via the existing editor_ids map.
    std::vector<std::pair<std::string, uint32_t>> synthetic_editor_ids;

    // Total lines parsed across all *_KID.ini files. Used for
    // user-facing logging (`KID INI: N lines -> M rules`).
    size_t lines_parsed = 0;
};

// Discover all `*_KID.ini` files under `inputs.data_dir` (or `kid_dir`
// if set), parse them, resolve references, and synthesize Mora rules.
//
// Diagnostics surface through `diags`. The function does not enforce
// any pre-existing module dependencies — the synthesized rules can
// reference Skyrim form predicates regardless of which user modules
// are loaded.
//
// `inputs.editor_ids` must be non-null and populated; an empty map
// produces a warning and zero rules.
KidCompileResult compile_kid_modules(
    const KidCompileInputs& inputs,
    mora::StringPool&       pool,
    mora::DiagBag&          diags);

} // namespace mora_skyrim_compile
