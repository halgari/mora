#pragma once

namespace mora::ext { class ExtensionContext; }

namespace mora_skyrim_compile {

// Register all Skyrim nominal type tags (FormID, NpcID, WeaponID, ...)
// with the given ExtensionContext. Must be called before
// register_defaults so the schema-building code can resolve type names.
void register_all_nominal_types(mora::ext::ExtensionContext& ctx);

} // namespace mora_skyrim_compile
