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
// SkyrimEspDataSource (compile_kid_modules reads it for EditorID-shape
// refs + wildcard expansion).
struct KidCompileInputs {
    std::filesystem::path                                   data_dir;
    std::filesystem::path                                   kid_dir;          // empty = use data_dir
    const std::unordered_map<std::string, uint32_t>*        editor_ids = nullptr;
};

// Output of KID rule synthesis.
struct KidCompileResult {
    // Synthesized module containing one rule per KID line per OR-group.
    // Empty (no rules, no namespace) when KID is disabled, no
    // *_KID.ini files exist, or all lines failed to resolve. FormID-ref
    // refs appear as TaggedLiteralExpr("form", "0xNNN@Plugin.ext") in
    // the AST — the `#form` reader globalizes them during reader
    // expansion.
    mora::Module module;

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
