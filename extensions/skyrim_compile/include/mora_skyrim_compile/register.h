#pragma once

#include "mora/ext/extension.h"

namespace mora_skyrim_compile {

// Foundation stub entry point for the compile-time Skyrim extension.
// Will register nominal types, relation schemas, ESP data source, and
// LSP enrichment providers. Called from main() after ExtensionContext
// construction.
void register_skyrim(mora::ext::ExtensionContext& ctx);

} // namespace mora_skyrim_compile
