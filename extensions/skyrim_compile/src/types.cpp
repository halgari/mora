#include "mora_skyrim_compile/types.h"

#include "mora/core/type.h"
#include "mora/data/value.h"
#include "mora/ext/extension.h"

namespace mora_skyrim_compile {

void register_all_nominal_types(mora::ext::ExtensionContext& ctx) {
    // FormID is the base nominal; per-form-type subtypes layer over the
    // same physical (Int32). All 12 Skyrim nominals decode as Kind::FormID
    // at the Value level — Mora has no NpcID/WeaponID/etc. kind.
    auto const* i32 = mora::types::int32();
    auto const  hint = mora::Value::Kind::FormID;

    ctx.register_nominal_type("FormID",     i32, hint);
    ctx.register_nominal_type("NpcID",      i32, hint);
    ctx.register_nominal_type("WeaponID",   i32, hint);
    ctx.register_nominal_type("ArmorID",    i32, hint);
    ctx.register_nominal_type("KeywordID",  i32, hint);
    ctx.register_nominal_type("FactionID",  i32, hint);
    ctx.register_nominal_type("SpellID",    i32, hint);
    ctx.register_nominal_type("PerkID",     i32, hint);
    ctx.register_nominal_type("QuestID",    i32, hint);
    ctx.register_nominal_type("LocationID", i32, hint);
    ctx.register_nominal_type("CellID",     i32, hint);
    ctx.register_nominal_type("RaceID",     i32, hint);
}

} // namespace mora_skyrim_compile
