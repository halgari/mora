#pragma once
#include <nlohmann/json.hpp>

#include "mora/diag/diagnostic.h"

namespace mora::lsp {

// Convert a single mora Diagnostic into an LSP Diagnostic JSON object.
// The mora::SourceSpan is 1-based; the LSP wants 0-based, so we subtract 1
// from line/character.
nlohmann::json diagnostic_to_json(const mora::Diagnostic& d);

} // namespace mora::lsp
